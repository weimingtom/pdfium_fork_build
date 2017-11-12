// MyPdfium.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "fpdfsdk/include/fpdf_dataavail.h"
#include "fpdfsdk/include/fpdf_ext.h"
#include "fpdfsdk/include/fpdftext.h"
#include "fpdfsdk/include/fpdfview.h"




enum OutputFormat {
	OUTPUT_NONE,
	OUTPUT_PPM,
	OUTPUT_PNG,
#ifdef _WIN32
	OUTPUT_BMP,
	OUTPUT_EMF,
#endif
};

struct Options {
	Options() : output_format(OUTPUT_NONE) { }

	OutputFormat output_format;
	std::string scale_factor_as_string;
	std::string exe_path;
	std::string bin_directory;
};

// Reads the entire contents of a file into a newly malloc'd buffer.
static char* GetFileContents(const char* filename, size_t* retlen) {
	FILE* file = fopen(filename, "rb");
	if (!file) {
		fprintf(stderr, "Failed to open: %s\n", filename);
		return NULL;
	}
	(void) fseek(file, 0, SEEK_END);
	size_t file_length = ftell(file);
	if (!file_length) {
		return NULL;
	}
	(void) fseek(file, 0, SEEK_SET);
	char* buffer = (char*) malloc(file_length);
	if (!buffer) {
		return NULL;
	}
	size_t bytes_read = fread(buffer, 1, file_length, file);
	(void) fclose(file);
	if (bytes_read != file_length) {
		fprintf(stderr, "Failed to read: %s\n", filename);
		free(buffer);
		return NULL;
	}
	*retlen = bytes_read;
	return buffer;
}


static bool CheckDimensions(int stride, int width, int height) {
	if (stride < 0 || width < 0 || height < 0)
		return false;
	if (height > 0 && width > INT_MAX / height)
		return false;
	return true;
}



#ifdef _WIN32
static void WriteBmp(const char* pdf_name, int num, const void* buffer,
	int stride, int width, int height) {
		if (stride < 0 || width < 0 || height < 0)
			return;
		if (height > 0 && width > INT_MAX / height)
			return;
		int out_len = stride * height;
		if (out_len > INT_MAX / 3)
			return;

		char filename[256];
		sprintf(filename, "%s.%d.bmp", pdf_name, num);
		FILE* fp = fopen(filename, "wb");
		if (!fp)
			return;

		BITMAPINFO bmi = {0};
		bmi.bmiHeader.biSize = sizeof(bmi) - sizeof(RGBQUAD);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height;  // top-down image
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = 0;

		BITMAPFILEHEADER file_header = {0};
		file_header.bfType = 0x4d42;
		file_header.bfSize = sizeof(file_header) + bmi.bmiHeader.biSize + out_len;
		file_header.bfOffBits = file_header.bfSize - out_len;

		fwrite(&file_header, sizeof(file_header), 1, fp);
		fwrite(&bmi, bmi.bmiHeader.biSize, 1, fp);
		fwrite(buffer, out_len, 1, fp);
		fclose(fp);
}

void WriteEmf(FPDF_PAGE page, const char* pdf_name, int num) {
	int width = static_cast<int>(FPDF_GetPageWidth(page));
	int height = static_cast<int>(FPDF_GetPageHeight(page));

	char filename[256];
	sprintf(filename, "%s.%d.emf", pdf_name, num);

	HDC dc = CreateEnhMetaFileA(NULL, filename, NULL, NULL);

	HRGN rgn = CreateRectRgn(0, 0, width, height);
	SelectClipRgn(dc, rgn);
	DeleteObject(rgn);

	SelectObject(dc, GetStockObject(NULL_PEN));
	SelectObject(dc, GetStockObject(WHITE_BRUSH));
	// If a PS_NULL pen is used, the dimensions of the rectangle are 1 pixel less.
	Rectangle(dc, 0, 0, width + 1, height + 1);

	FPDF_RenderPage(dc, page, 0, 0, width, height, 0,
		FPDF_ANNOT | FPDF_PRINTING | FPDF_NO_CATCH);

	DeleteEnhMetaFile(CloseEnhMetaFile(dc));
}
#endif


void ExampleUnsupportedHandler(UNSUPPORT_INFO*, int type) {
	std::string feature = "Unknown";
	switch (type) {
	case FPDF_UNSP_DOC_XFAFORM:
		feature = "XFA";
		break;
	case FPDF_UNSP_DOC_PORTABLECOLLECTION:
		feature = "Portfolios_Packages";
		break;
	case FPDF_UNSP_DOC_ATTACHMENT:
	case FPDF_UNSP_ANNOT_ATTACHMENT:
		feature = "Attachment";
		break;
	case FPDF_UNSP_DOC_SECURITY:
		feature = "Rights_Management";
		break;
	case FPDF_UNSP_DOC_SHAREDREVIEW:
		feature = "Shared_Review";
		break;
	case FPDF_UNSP_DOC_SHAREDFORM_ACROBAT:
	case FPDF_UNSP_DOC_SHAREDFORM_FILESYSTEM:
	case FPDF_UNSP_DOC_SHAREDFORM_EMAIL:
		feature = "Shared_Form";
		break;
	case FPDF_UNSP_ANNOT_3DANNOT:
		feature = "3D";
		break;
	case FPDF_UNSP_ANNOT_MOVIE:
		feature = "Movie";
		break;
	case FPDF_UNSP_ANNOT_SOUND:
		feature = "Sound";
		break;
	case FPDF_UNSP_ANNOT_SCREEN_MEDIA:
	case FPDF_UNSP_ANNOT_SCREEN_RICHMEDIA:
		feature = "Screen";
		break;
	case FPDF_UNSP_ANNOT_SIG:
		feature = "Digital_Signature";
		break;
	}
	printf("Unsupported feature: %s.\n", feature.c_str());
}

bool ParseCommandLine(const std::vector<std::string>& args,
	Options* options, std::list<std::string>* files) {
		if (args.empty()) {
			return false;
		}
		options->exe_path = args[0];
		size_t cur_idx = 1;
		for (; cur_idx < args.size(); ++cur_idx) {
			const std::string& cur_arg = args[cur_idx];
			if (cur_arg == "--ppm") {
				if (options->output_format != OUTPUT_NONE) {
					fprintf(stderr, "Duplicate or conflicting --ppm argument\n");
					return false;
				}
				options->output_format = OUTPUT_PPM;
			} else if (cur_arg == "--png") {
				if (options->output_format != OUTPUT_NONE) {
					fprintf(stderr, "Duplicate or conflicting --png argument\n");
					return false;
				}
				options->output_format = OUTPUT_PNG;
			}
#ifdef _WIN32
			else if (cur_arg == "--emf") {
				if (options->output_format != OUTPUT_NONE) {
					fprintf(stderr, "Duplicate or conflicting --emf argument\n");
					return false;
				}
				options->output_format = OUTPUT_EMF;
			}
			else if (cur_arg == "--bmp") {
				if (options->output_format != OUTPUT_NONE) {
					fprintf(stderr, "Duplicate or conflicting --bmp argument\n");
					return false;
				}
				options->output_format = OUTPUT_BMP;
			}
#endif  // _WIN32
			else if (cur_arg.size() > 8 && cur_arg.compare(0, 8, "--scale=") == 0) {
				if (!options->scale_factor_as_string.empty()) {
					fprintf(stderr, "Duplicate --scale argument\n");
					return false;
				}
				options->scale_factor_as_string = cur_arg.substr(8);
			}
			else
				break;
		}
		if (cur_idx >= args.size()) {
			fprintf(stderr, "No input files.\n");
			return false;
		}
		for (size_t i = cur_idx; i < args.size(); i++) {
			files->push_back(args[i]);
		}
		return true;
}

class TestLoader {
public:
	TestLoader(const char* pBuf, size_t len);

	const char* m_pBuf;
	size_t m_Len;
};

TestLoader::TestLoader(const char* pBuf, size_t len)
	: m_pBuf(pBuf), m_Len(len) {
}

int Get_Block(void* param, unsigned long pos, unsigned char* pBuf,
	unsigned long size) {
		TestLoader* pLoader = (TestLoader*) param;
		if (pos + size < pos || pos + size > pLoader->m_Len) return 0;
		memcpy(pBuf, pLoader->m_pBuf + pos, size);
		return 1;
}

bool Is_Data_Avail(FX_FILEAVAIL* pThis, size_t offset, size_t size) {
	return true;
}

void Add_Segment(FX_DOWNLOADHINTS* pThis, size_t offset, size_t size) {
}

void RenderPdf(const std::string& name, const char* pBuf, size_t len,
	const Options& options) {
		fprintf(stderr, "Rendering PDF file %s.\n", name.c_str());	

		TestLoader loader(pBuf, len);

		FPDF_FILEACCESS file_access;
		memset(&file_access, '\0', sizeof(file_access));
		file_access.m_FileLen = static_cast<unsigned long>(len);
		file_access.m_GetBlock = Get_Block;
		file_access.m_Param = &loader;

		FX_FILEAVAIL file_avail;
		memset(&file_avail, '\0', sizeof(file_avail));
		file_avail.version = 1;
		file_avail.IsDataAvail = Is_Data_Avail;

		FX_DOWNLOADHINTS hints;
		memset(&hints, '\0', sizeof(hints));
		hints.version = 1;
		hints.AddSegment = Add_Segment;

		FPDF_DOCUMENT doc;
		FPDF_AVAIL pdf_avail = FPDFAvail_Create(&file_avail, &file_access);

		(void) FPDFAvail_IsDocAvail(pdf_avail, &hints);

		if (!FPDFAvail_IsLinearized(pdf_avail)) {
			fprintf(stderr, "Non-linearized path...\n");
			doc = FPDF_LoadCustomDocument(&file_access, NULL);
		} else {
			fprintf(stderr, "Linearized path...\n");
			doc = FPDFAvail_GetDocument(pdf_avail, NULL);
		}

		(void) FPDF_GetDocPermissions(doc);
		(void) FPDFAvail_IsFormAvail(pdf_avail, &hints);
		

		int first_page = FPDFAvail_GetFirstPageNum(doc);
		(void) FPDFAvail_IsPageAvail(pdf_avail, first_page, &hints);

		int page_count = FPDF_GetPageCount(doc);
		for (int i = 0; i < page_count; ++i) {
			(void) FPDFAvail_IsPageAvail(pdf_avail, i, &hints);
		}

		int rendered_pages = 0;
		int bad_pages = 0;
		for (int i = 0; i < page_count; ++i) {
			FPDF_PAGE page = FPDF_LoadPage(doc, i);
			if (!page) {
				bad_pages ++;
				continue;
			}
			FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);

			double scale = 1.0;
			if (!options.scale_factor_as_string.empty()) {
				std::stringstream(options.scale_factor_as_string) >> scale;
			}
			int width = static_cast<int>(FPDF_GetPageWidth(page) * scale);
			int height = static_cast<int>(FPDF_GetPageHeight(page) * scale);

			FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 0);
			if (!bitmap) {
				fprintf(stderr, "Page was too large to be rendered.\n");
				bad_pages++;
				continue;
			}

			FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFF,0xFF,0xFF,0xFF);
			FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, 0);
			rendered_pages ++;
	
			int stride = FPDFBitmap_GetStride(bitmap);
			const char* buffer =
				reinterpret_cast<const char*>(FPDFBitmap_GetBuffer(bitmap));

			switch (options.output_format) {
#ifdef _WIN32
			case OUTPUT_BMP:
				WriteBmp(name.c_str(), i, buffer, stride, width, height);
				break;

			case OUTPUT_EMF:
				WriteEmf(page, name.c_str(), i);
				break;
#endif
			default:
				break;
			}

			FPDFBitmap_Destroy(bitmap);
			
			FPDFText_ClosePage(text_page);
			FPDF_ClosePage(page);
		}

		FPDF_CloseDocument(doc);
		FPDFAvail_Destroy(pdf_avail);

		fprintf(stderr, "Rendered %d pages.\n", rendered_pages);
		fprintf(stderr, "Skipped %d bad pages.\n", bad_pages);
}

static const char usage_string[] =
	"Usage: pdfium_test [OPTION] [FILE]...\n"
	"  --bin-dir=<path> - override path to v8 external data\n"
	"  --scale=<number> - scale output size by number (e.g. 0.5)\n"
#ifdef _WIN32
	"  --bmp - write page images <pdf-name>.<page-number>.bmp\n"
	"  --emf - write page meta files <pdf-name>.<page-number>.emf\n"
#endif
	"  --png - write page images <pdf-name>.<page-number>.png\n"
	"  --ppm - write page images <pdf-name>.<page-number>.ppm\n";

int main(int argc, const char* argv[])
{

	std::vector<std::string> args(argv, argv + argc);
	Options options;
	std::list<std::string> files;
	if (!ParseCommandLine(args, &options, &files)) {
		fprintf(stderr, "%s", usage_string);
		return 1;
	}



	FPDF_InitLibrary(NULL);

	UNSUPPORT_INFO unsuppored_info;
	memset(&unsuppored_info, '\0', sizeof(unsuppored_info));
	unsuppored_info.version = 1;
	unsuppored_info.FSDK_UnSupport_Handler = ExampleUnsupportedHandler;

	FSDK_SetUnSpObjProcessHandler(&unsuppored_info);

	while (!files.empty()) {
		std::string filename = files.front();
		files.pop_front();
		size_t file_length = 0;
		char* file_contents = GetFileContents(filename.c_str(), &file_length);
		if (!file_contents)
			continue;
		RenderPdf(filename, file_contents, file_length, options);
		free(file_contents);
	}

	FPDF_DestroyLibrary();
		
	return 0;
}
