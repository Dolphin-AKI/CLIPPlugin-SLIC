//! TriglavPlugIn SDK
//! Copyright (c) CELSYS Inc.
//! All Rights Reserved.
#include "TriglavPlugInSDK/TriglavPlugInSDK.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <limits>
#include <fstream>
#include <iostream>
#include <exception>


#define ENABLE_LOGGING 0 // 1: Enable logging, 0: Disable logging

static void Log(const std::string& msg) {
#if ENABLE_LOGGING
	try {
		std::ofstream outfile("C:\\Temp\\slic_debug.txt", std::ios_base::app);
		if (outfile.is_open()) {
			outfile << msg << std::endl;
		}
	} catch (...) {}
#endif
}

/*--------------------------------------------------------------------------
Copyright (c) 2025 Akihiro.Watanabe

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
--------------------------------------------------------------------------- */

// Property Keys
static const int kItemKeyCellSize = 1;
static const int kItemKeyCompactness = 2;

// String IDs (Must match localized strings if used, or just be unique)
static const int kStringIDFilterCategoryName = 101;
static const int kStringIDFilterName = 102;
static const int kStringIDItemCaptionCellSize = 103;
static const int kStringIDItemCaptionCompactness = 104;

typedef unsigned char BYTE;

// Filter Info
struct SLICFilterInfo
{
	TriglavPlugInInt cellSize;
	TriglavPlugInDouble compactness;
	TriglavPlugInPropertyService* pPropertyService;
};

// Property Callback
static void TRIGLAV_PLUGIN_CALLBACK TriglavPlugInFilterPropertyCallBack(TriglavPlugInInt* result, TriglavPlugInPropertyObject propertyObject, const TriglavPlugInInt itemKey, const TriglavPlugInInt notify, TriglavPlugInPtr data)
{
	(*result) = kTriglavPlugInPropertyCallBackResultNoModify;

	SLICFilterInfo* pFilterInfo = static_cast<SLICFilterInfo*>(data);
	if (pFilterInfo != NULL && pFilterInfo->pPropertyService != NULL)
	{
		if (notify == kTriglavPlugInPropertyCallBackNotifyValueChanged)
		{
			if (itemKey == kItemKeyCellSize)
			{
				TriglavPlugInInt value;
				pFilterInfo->pPropertyService->getIntegerValueProc(&value, propertyObject, itemKey);
				if (pFilterInfo->cellSize != value)
				{
					pFilterInfo->cellSize = value;
					(*result) = kTriglavPlugInPropertyCallBackResultModify;
				}
			}
			else if (itemKey == kItemKeyCompactness)
			{
				TriglavPlugInDouble value;
				pFilterInfo->pPropertyService->getDecimalValueProc(&value, propertyObject, itemKey);
				if (std::abs(pFilterInfo->compactness - value) > 1e-6)
				{
					pFilterInfo->compactness = value;
					(*result) = kTriglavPlugInPropertyCallBackResultModify;
				}
			}
		}
	}
}

// --- SLIC Implementation ---

struct SlicColor {
	double l, a, b;
};

struct SlicCluster {
	double l, a, b;
	double x, y;
	int count;
};

// RGB to LAB conversion
static void RGB2LAB(BYTE r, BYTE g, BYTE b, double& lVal, double& aVal, double& bVal)
{
	double var_R = (r / 255.0);
	double var_G = (g / 255.0);
	double var_B = (b / 255.0);

	if (var_R > 0.04045) var_R = pow((var_R + 0.055) / 1.055, 2.4);
	else                 var_R = var_R / 12.92;
	if (var_G > 0.04045) var_G = pow((var_G + 0.055) / 1.055, 2.4);
	else                 var_G = var_G / 12.92;
	if (var_B > 0.04045) var_B = pow((var_B + 0.055) / 1.055, 2.4);
	else                 var_B = var_B / 12.92;

	var_R = var_R * 100.0;
	var_G = var_G * 100.0;
	var_B = var_B * 100.0;

	double X = var_R * 0.4124 + var_G * 0.3576 + var_B * 0.1805;
	double Y = var_R * 0.2126 + var_G * 0.7152 + var_B * 0.0722;
	double Z = var_R * 0.0193 + var_G * 0.1192 + var_B * 0.9505;

	double var_X = X / 95.047;
	double var_Y = Y / 100.000;
	double var_Z = Z / 108.883;

	if (var_X > 0.008856) var_X = pow(var_X, (1.0 / 3.0));
	else                  var_X = (7.787 * var_X) + (16.0 / 116.0);
	if (var_Y > 0.008856) var_Y = pow(var_Y, (1.0 / 3.0));
	else                  var_Y = (7.787 * var_Y) + (16.0 / 116.0);
	if (var_Z > 0.008856) var_Z = pow(var_Z, (1.0 / 3.0));
	else                  var_Z = (7.787 * var_Z) + (16.0 / 116.0);

	lVal = (116.0 * var_Y) - 16.0;
	aVal = 500.0 * (var_X - var_Y);
	bVal = 200.0 * (var_Y - var_Z);
}

// LAB to RGB conversion
static void LAB2RGB(double lVal, double aVal, double bVal, BYTE& r, BYTE& g, BYTE& b)
{
	double var_Y = (lVal + 16.0) / 116.0;
	double var_X = aVal / 500.0 + var_Y;
	double var_Z = var_Y - bVal / 200.0;

	if (pow(var_Y, 3) > 0.008856) var_Y = pow(var_Y, 3);
	else                          var_Y = (var_Y - 16.0 / 116.0) / 7.787;
	if (pow(var_X, 3) > 0.008856) var_X = pow(var_X, 3);
	else                          var_X = (var_X - 16.0 / 116.0) / 7.787;
	if (pow(var_Z, 3) > 0.008856) var_Z = pow(var_Z, 3);
	else                          var_Z = (var_Z - 16.0 / 116.0) / 7.787;

	double X = var_X * 95.047;
	double Y = var_Y * 100.000;
	double Z = var_Z * 108.883;

	double var_R = X * 3.2406 + Y * -1.5372 + Z * -0.4986;
	double var_G = X * -0.9689 + Y * 1.8758 + Z * 0.0415;
	double var_B = X * 0.0557 + Y * -0.2040 + Z * 1.0570;

	var_R = var_R / 100.0;
	var_G = var_G / 100.0;
	var_B = var_B / 100.0;

	if (var_R > 0.0031308) var_R = 1.055 * pow(var_R, (1.0 / 2.4)) - 0.055;
	else                   var_R = 12.92 * var_R;
	if (var_G > 0.0031308) var_G = 1.055 * pow(var_G, (1.0 / 2.4)) - 0.055;
	else                   var_G = 12.92 * var_G;
	if (var_B > 0.0031308) var_B = 1.055 * pow(var_B, (1.0 / 2.4)) - 0.055;
	else                   var_B = 12.92 * var_B;

	var_R = std::max(0.0, std::min(1.0, var_R));
	var_G = std::max(0.0, std::min(1.0, var_G));
	var_B = std::max(0.0, std::min(1.0, var_B));

	r = (BYTE)(var_R * 255.0);
	g = (BYTE)(var_G * 255.0);
	b = (BYTE)(var_B * 255.0);
}

class SLICProcessor {
public:
	TriglavPlugInInt width, height;
	std::vector<SlicColor> labData;
	std::vector<int> labels;
	std::vector<double> distances;
	std::vector<SlicCluster> clusters;
	std::vector<BYTE> resultRGB; // Storing final RGB to quickly serve blocks
	std::vector<bool> validPixels;

	// Modified Initialize to take raw pointers
	void Initialize(TriglavPlugInInt w, TriglavPlugInInt h, const BYTE* srcBuffer, TriglavPlugInInt rowBytes, TriglavPlugInInt pixelBytes) {
		width = w;
		height = h;
		size_t totalPixels = (size_t)w * (size_t)h;
		labData.resize(totalPixels);
		labels.assign(totalPixels, -1);
		distances.assign(totalPixels, std::numeric_limits<double>::max());
		resultRGB.resize(totalPixels * 4); // Keep it RGBA
		validPixels.resize(totalPixels);

		// Convert Input to Lab
		for (TriglavPlugInInt y = 0; y < h; y++) {
			const BYTE* srcRow = srcBuffer + (y * rowBytes);
			for (TriglavPlugInInt x = 0; x < w; x++) {
				size_t idx = (size_t)y * w + x;
				const BYTE* px = srcRow + (x * pixelBytes);
				
				// Assumes pixelBytes >= 4 for RGBA, or at least 3 for RGB
				BYTE r = px[0];
				BYTE g = px[1];
				BYTE b = px[2];
				BYTE alpha = (pixelBytes >= 4) ? px[3] : 255;

				double l, a, b_val; // b_val to avoid conflict with 'b'
				RGB2LAB(r, g, b, l, a, b_val);
				labData[idx] = { l, a, b_val };

				validPixels[idx] = (alpha != 0);

				// Initialize result with original
				resultRGB[idx*4+0] = r;
				resultRGB[idx*4+1] = g;
				resultRGB[idx*4+2] = b;
				resultRGB[idx*4+3] = alpha;
			}
		}
	}

	TriglavPlugInInt Execute(int step, double m, TriglavPlugInRecordSuite* pRecordSuite, TriglavPlugInHostObject hostObject, TriglavPlugInInt* pCurrentProgress, TriglavPlugInInt progressUnit) {
		if (step < 2) step = 2; // min step

		// 1. Initialize Centers
		clusters.clear();
		for (TriglavPlugInInt y = step / 2; y < height; y += step) {
			for (TriglavPlugInInt x = step / 2; x < width; x += step) {
				TriglavPlugInInt cx = x;
				TriglavPlugInInt cy = y;
				size_t centerIdx = (size_t)cy * width + cx;

				// If grid center is transparent (invalid), search neighbors for a valid spot
				if (!validPixels[centerIdx]) {
					bool found = false;
					int searchRange = step / 2; 

					TriglavPlugInInt startY = std::max<TriglavPlugInInt>(0, y - searchRange);
					TriglavPlugInInt endY = std::min<TriglavPlugInInt>(height, y + searchRange);
					TriglavPlugInInt startX = std::max<TriglavPlugInInt>(0, x - searchRange);
					TriglavPlugInInt endX = std::min<TriglavPlugInInt>(width, x + searchRange);

					for (TriglavPlugInInt ny = startY; ny < endY; ny++) {
						for (TriglavPlugInInt nx = startX; nx < endX; nx++) {
							if (validPixels[(size_t)ny * width + nx]) {
								// Found a valid pixel
								cx = nx;
								cy = ny;
								centerIdx = (size_t)ny * width + nx;
								found = true;
								break;
							}
						}
						if (found) break;
					}
					// If no valid pixel found in neighborhood, skip this cluster
					if (!found) continue;
				}

				SlicColor c = labData[centerIdx];
				clusters.push_back({ c.l, c.a, c.b, (double)cx, (double)cy, 0 });
			}
		}

		int ns = step;
		// 2. Iterations
		for (int iter = 0; iter < 10; iter++) {
			// Update Progress for Iteration
			if (pCurrentProgress) {
				*pCurrentProgress += progressUnit;
				TriglavPlugInFilterRunSetProgressDone(pRecordSuite, hostObject, *pCurrentProgress);
			}

			TriglavPlugInInt processResult = 0;
			TriglavPlugInFilterRunProcess(pRecordSuite, &processResult, hostObject, kTriglavPlugInFilterRunProcessStateContinue);
			if (processResult == kTriglavPlugInFilterRunProcessResultExit || processResult == kTriglavPlugInFilterRunProcessResultRestart) {
				return processResult;
			}

			// Assignment
			for (int k = 0; k < (int)clusters.size(); k++) {
				int cx = (int)clusters[k].x;
				int cy = (int)clusters[k].y;
				
				// Search region 2S x 2S
				TriglavPlugInInt startX = std::max<TriglavPlugInInt>(0, cx - ns);
				TriglavPlugInInt startY = std::max<TriglavPlugInInt>(0, cy - ns);
				TriglavPlugInInt endX = std::min<TriglavPlugInInt>(width, cx + ns);
				TriglavPlugInInt endY = std::min<TriglavPlugInInt>(height, cy + ns);

				for (TriglavPlugInInt y = startY; y < endY; y++) {
					for (TriglavPlugInInt x = startX; x < endX; x++) {
						size_t idx = (size_t)y * width + x;
						if (!validPixels[idx]) continue;

						SlicColor pixel = labData[idx];
						
						double d_lab = std::pow(pixel.l - clusters[k].l, 2) + 
									   std::pow(pixel.a - clusters[k].a, 2) + 
									   std::pow(pixel.b - clusters[k].b, 2);
						
						double d_xy = std::pow(x - clusters[k].x, 2) + 
									  std::pow(y - clusters[k].y, 2);
						
						double D = d_lab + (m * m / (ns * ns)) * d_xy;

						if (D < distances[idx]) {
							distances[idx] = D;
							labels[idx] = k;
						}
					}
				}
			}

			// Restore previous clusters to handle empty ones
			std::vector<SlicCluster> prevClusters = clusters;

			// Update
			for (auto& c : clusters) {
				c.l = c.a = c.b = c.x = c.y = 0.0;
				c.count = 0;
			}

			for (size_t i = 0; i < (size_t)width * height; i++) {
				if (!validPixels[i]) continue;
				int k = labels[i];
				if (k >= 0 && k < (int)clusters.size()) {
					clusters[k].l += labData[i].l;
					clusters[k].a += labData[i].a;
					clusters[k].b += labData[i].b;
					clusters[k].x += (i % width);
					clusters[k].y += (i / width);
					clusters[k].count++;
				}
			}

			// Average
			for (size_t k = 0; k < clusters.size(); k++) {
				if (clusters[k].count > 0) {
					clusters[k].l /= clusters[k].count;
					clusters[k].a /= clusters[k].count;
					clusters[k].b /= clusters[k].count;
					clusters[k].x /= clusters[k].count;
					clusters[k].y /= clusters[k].count;
				} else {
					// Restore previous state if empty to prevent zeroing/black blocks
					clusters[k] = prevClusters[k];
				}
			}
			
			// Reset distances for next iter (except last one)
			if (iter < 9) {
				distances.assign((size_t)width * height, std::numeric_limits<double>::max());
			}
		}
		
		// 3. Render Output
		// Update Progress for Render
		if (pCurrentProgress) {
			*pCurrentProgress += progressUnit;
			TriglavPlugInFilterRunSetProgressDone(pRecordSuite, hostObject, *pCurrentProgress);
		}

		for (size_t i = 0; i < (size_t)width * height; i++) {
			int k = labels[i];
			if (k >= 0 && k < (int)clusters.size()) {
				BYTE r, g, b;
				LAB2RGB(clusters[k].l, clusters[k].a, clusters[k].b, r, g, b);
				resultRGB[i * 4 + 0] = r;
				resultRGB[i * 4 + 1] = g;
				resultRGB[i * 4 + 2] = b;
				// resultRGB[i * 4 + 3] is already original alpha or 255
			}
		}
		return kTriglavPlugInFilterRunProcessResultContinue;
	}
};


//	Main Entry Point
void TRIGLAV_PLUGIN_API TriglavPluginCall(TriglavPlugInInt* result, TriglavPlugInPtr* data, TriglavPlugInInt selector, TriglavPlugInServer* pluginServer, TriglavPlugInPtr reserved)
{
	*result = kTriglavPlugInCallResultFailed;
	if (pluginServer != NULL)
	{
		if (selector == kTriglavPlugInSelectorModuleInitialize)
		{
			TriglavPlugInModuleInitializeRecord* pModuleInitializeRecord = (*pluginServer).recordSuite.moduleInitializeRecord;
			TriglavPlugInStringService* pStringService = (*pluginServer).serviceSuite.stringService;
			if (pModuleInitializeRecord != NULL && pStringService != NULL)
			{
				TriglavPlugInInt	hostVersion;
				(*pModuleInitializeRecord).getHostVersionProc(&hostVersion, (*pluginServer).hostObject);
				if (hostVersion >= kTriglavPlugInNeedHostVersion)
				{
					TriglavPlugInStringObject	moduleID = NULL;
					const char* moduleIDString = "B4D8E92C-SLIC-4388-8927-0B6BDAFAA4DA"; // Use Unique ID
					(*pStringService).createWithAsciiStringProc(&moduleID, moduleIDString, static_cast<TriglavPlugInInt>(::strlen(moduleIDString)));
					(*pModuleInitializeRecord).setModuleIDProc((*pluginServer).hostObject, moduleID);
					(*pModuleInitializeRecord).setModuleKindProc((*pluginServer).hostObject, kTriglavPlugInModuleSwitchKindFilter);
					(*pStringService).releaseProc(moduleID);

					SLICFilterInfo* pFilterInfo = new SLICFilterInfo;
					pFilterInfo->cellSize = 30;
					pFilterInfo->compactness = 20.0;
					*data = pFilterInfo;
					*result = kTriglavPlugInCallResultSuccess;
				}
			}
		}
		else if (selector == kTriglavPlugInSelectorModuleTerminate)
		{
			SLICFilterInfo* pFilterInfo = static_cast<SLICFilterInfo*>(*data);
			if (pFilterInfo) delete pFilterInfo;
			*data = NULL;
			*result = kTriglavPlugInCallResultSuccess;
		}
		else if (selector == kTriglavPlugInSelectorFilterInitialize)
		{
			TriglavPlugInRecordSuite* pRecordSuite = &(*pluginServer).recordSuite;
			TriglavPlugInHostObject hostObject = (*pluginServer).hostObject;
			TriglavPlugInStringService* pStringService = (*pluginServer).serviceSuite.stringService;
			TriglavPlugInPropertyService* pPropertyService = (*pluginServer).serviceSuite.propertyService;

			if (TriglavPlugInGetFilterInitializeRecord(pRecordSuite) != NULL && pStringService != NULL && pPropertyService != NULL)
			{
				TriglavPlugInStringObject filterCategoryName = NULL;
				TriglavPlugInStringObject filterName = NULL;
				(*pStringService).createWithStringIDProc(&filterCategoryName, kStringIDFilterCategoryName, hostObject);
				(*pStringService).createWithStringIDProc(&filterName, kStringIDFilterName, hostObject);

				TriglavPlugInFilterInitializeSetFilterCategoryName(pRecordSuite, hostObject, filterCategoryName, 'c');
				TriglavPlugInFilterInitializeSetFilterName(pRecordSuite, hostObject, filterName, 's');
				(*pStringService).releaseProc(filterCategoryName);
				(*pStringService).releaseProc(filterName);

				TriglavPlugInFilterInitializeSetCanPreview(pRecordSuite, hostObject, true);

				TriglavPlugInInt target[] = { kTriglavPlugInFilterTargetKindRasterLayerRGBAlpha };
				TriglavPlugInFilterInitializeSetTargetKinds(pRecordSuite, hostObject, target, 1);

				TriglavPlugInPropertyObject propertyObject;
				(*pPropertyService).createProc(&propertyObject);

				// Cell Size (Integer)
				TriglavPlugInStringObject cellSizeCaption = NULL;
				(*pStringService).createWithStringIDProc(&cellSizeCaption, kStringIDItemCaptionCellSize, hostObject);
				(*pPropertyService).addItemProc(propertyObject, kItemKeyCellSize, kTriglavPlugInPropertyValueTypeInteger, kTriglavPlugInPropertyValueKindDefault, kTriglavPlugInPropertyInputKindDefault, cellSizeCaption, 'z');
				(*pPropertyService).setIntegerValueProc(propertyObject, kItemKeyCellSize, 30);
				(*pPropertyService).setIntegerDefaultValueProc(propertyObject, kItemKeyCellSize, 30);
				(*pPropertyService).setIntegerMinValueProc(propertyObject, kItemKeyCellSize, 5);
				(*pPropertyService).setIntegerMaxValueProc(propertyObject, kItemKeyCellSize, 200);
				(*pStringService).releaseProc(cellSizeCaption);

				// Compactness (Decimal)
				TriglavPlugInStringObject compactCaption = NULL;
				(*pStringService).createWithStringIDProc(&compactCaption, kStringIDItemCaptionCompactness, hostObject);
				(*pPropertyService).addItemProc(propertyObject, kItemKeyCompactness, kTriglavPlugInPropertyValueTypeDecimal, kTriglavPlugInPropertyValueKindDefault, kTriglavPlugInPropertyInputKindDefault, compactCaption, 'm');
				(*pPropertyService).setDecimalValueProc(propertyObject, kItemKeyCompactness, 20.0);
				(*pPropertyService).setDecimalDefaultValueProc(propertyObject, kItemKeyCompactness, 20.0);
				(*pPropertyService).setDecimalMinValueProc(propertyObject, kItemKeyCompactness, 0.1);
				(*pPropertyService).setDecimalMaxValueProc(propertyObject, kItemKeyCompactness, 100.0);
				(*pStringService).releaseProc(compactCaption);

				TriglavPlugInFilterInitializeSetProperty(pRecordSuite, hostObject, propertyObject);
				TriglavPlugInFilterInitializeSetPropertyCallBack(pRecordSuite, hostObject, TriglavPlugInFilterPropertyCallBack, *data);
				(*pPropertyService).releaseProc(propertyObject);

				*result = kTriglavPlugInCallResultSuccess;
			}
		}
		else if (selector == kTriglavPlugInSelectorFilterTerminate)
		{
			*result = kTriglavPlugInCallResultSuccess;
		}
		else if (selector == kTriglavPlugInSelectorFilterRun)
		{
			try {
				Log("FilterRun start");
				//	Filter Execution
				TriglavPlugInRecordSuite* pRecordSuite = &(*pluginServer).recordSuite;
				TriglavPlugInBitmapService* pBitmapService = (*pluginServer).serviceSuite.bitmapService;
				TriglavPlugInOffscreenService* pOffscreenService = (*pluginServer).serviceSuite.offscreenService;
				TriglavPlugInPropertyService* pPropertyService = (*pluginServer).serviceSuite.propertyService;

				if (TriglavPlugInGetFilterRunRecord(pRecordSuite) != NULL && pBitmapService != NULL && pOffscreenService != NULL && pPropertyService != NULL)
				{
					// Simple RAII wrapper for Bitmap to ensure release on exception or break
					struct ScopeBitmap {
						TriglavPlugInBitmapService* svc;
						TriglavPlugInBitmapObject bmp;
						ScopeBitmap(TriglavPlugInBitmapService* s) : svc(s), bmp(NULL) {}
						~ScopeBitmap() { Release(); }
						void Release() { if (bmp) { svc->releaseProc(bmp); bmp = NULL; } }
						TriglavPlugInBitmapObject* operator&() { Release(); return &bmp; }
						operator TriglavPlugInBitmapObject() { return bmp; }
					};

					TriglavPlugInPropertyObject propertyObject;
					TriglavPlugInFilterRunGetProperty(pRecordSuite, &propertyObject, (*pluginServer).hostObject);

					TriglavPlugInOffscreenObject sourceOffscreenObject;
					TriglavPlugInFilterRunGetSourceOffscreen(pRecordSuite, &sourceOffscreenObject, (*pluginServer).hostObject);

					TriglavPlugInOffscreenObject destinationOffscreenObject;
					TriglavPlugInFilterRunGetDestinationOffscreen(pRecordSuite, &destinationOffscreenObject, (*pluginServer).hostObject);

					// We use full extent instead of block iteration
					TriglavPlugInRect selectAreaRect;
					TriglavPlugInFilterRunGetSelectAreaRect(pRecordSuite, &selectAreaRect, (*pluginServer).hostObject);

					SLICFilterInfo* pFilterInfo = static_cast<SLICFilterInfo*>(*data);
					pFilterInfo->pPropertyService = pPropertyService;

					// Local processor instance
					SLICProcessor processor; 
					TriglavPlugInInt currentProgress = 0;
					bool restart = true;
					
					ScopeBitmap srcBitmap(pBitmapService);
					ScopeBitmap dstBitmap(pBitmapService);

					while (true)
					{
						if (restart)
						{
							Log("Restarting processing loop...");
							restart = false;
							TriglavPlugInInt processResult;
							TriglavPlugInFilterRunProcess(pRecordSuite, &processResult, (*pluginServer).hostObject, kTriglavPlugInFilterRunProcessStateStart);
							if (processResult == kTriglavPlugInFilterRunProcessResultExit) { break; }
							
							// Release previous if restart happened mid-way
							srcBitmap.Release();
							dstBitmap.Release();

							// 1. Get Parameters
							pPropertyService->getIntegerValueProc(&(pFilterInfo->cellSize), propertyObject, kItemKeyCellSize);
							pPropertyService->getDecimalValueProc(&(pFilterInfo->compactness), propertyObject, kItemKeyCompactness);
							Log("Parameters - CellSize: " + std::to_string(pFilterInfo->cellSize) + ", Compactness: " + std::to_string(pFilterInfo->compactness));

							// 2. Load Full Image -> Bitmap
							TriglavPlugInRect extent;
							(*pOffscreenService).getExtentRectProc(&extent, sourceOffscreenObject);
							
							TriglavPlugInInt width = extent.right - extent.left;
							TriglavPlugInInt height = extent.bottom - extent.top;
							
							Log("Layer Extent: " + std::to_string((long long)extent.left) + "," + std::to_string((long long)extent.top));
							Log("Bitmap Size: " + std::to_string(width) + "x" + std::to_string(height));

							if (width <= 0 || height <= 0) {
								Log("Invalid dimensions, breaking.");
								break;
							}
							
							// Create Source Bitmap (Note: Depth 4 bytes = RGBA 8bit per channel)
							if((*pBitmapService).createProc(&srcBitmap, width, height, 4, kTriglavPlugInBitmapScanlineHorizontalLeftTop) != kTriglavPlugInAPIResultSuccess) {
								Log("Failed to create src bitmap");
								break;
							}
							
							// Copy from Offscreen to Source Bitmap
							TriglavPlugInPoint srcPos = {extent.left, extent.top};
							TriglavPlugInPoint zeroPos = {0, 0};
							// Note: kTriglavPlugInOffscreenCopyModeImage is 0x02, Normal is 0x01
							if((*pOffscreenService).getBitmapProc(srcBitmap, &zeroPos, sourceOffscreenObject, &srcPos, width, height, kTriglavPlugInOffscreenCopyModeNormal) != kTriglavPlugInAPIResultSuccess) {
								Log("Failed to copy offscreen to src bitmap");
								break;
							}

							// Setup Progress
							TriglavPlugInFilterRunSetProgressTotal(pRecordSuite, (*pluginServer).hostObject, 12);
							currentProgress = 0;

							// 3. Process
							Log("Initializing Processor...");
							
							TriglavPlugInPtr srcRaw = NULL;
							(*pBitmapService).getAddressProc(&srcRaw, srcBitmap, &zeroPos);
							TriglavPlugInInt srcRowBytes = 0;
							(*pBitmapService).getRowBytesProc(&srcRowBytes, srcBitmap);
							TriglavPlugInInt srcPixelBytes = 0;
							(*pBitmapService).getPixelBytesProc(&srcPixelBytes, srcBitmap); 
							
							processor.Initialize(width, height, (BYTE*)srcRaw, srcRowBytes, srcPixelBytes);

							currentProgress = 1;
							TriglavPlugInFilterRunSetProgressDone(pRecordSuite, (*pluginServer).hostObject, currentProgress);

							TriglavPlugInInt execResult = processor.Execute(pFilterInfo->cellSize, pFilterInfo->compactness, pRecordSuite, (*pluginServer).hostObject, &currentProgress, 1);

							if (execResult == kTriglavPlugInFilterRunProcessResultRestart) {
								Log("Processor requested Restart");
								restart = true;
								continue;
							}
							if (execResult == kTriglavPlugInFilterRunProcessResultExit) {
								Log("Processor requested Exit");
								break;
							}
							Log("Processor Done.");
							
							// 4. Create Result Bitmap
							if((*pBitmapService).createProc(&dstBitmap, width, height, 4, kTriglavPlugInBitmapScanlineHorizontalLeftTop) != kTriglavPlugInAPIResultSuccess) {
								Log("Failed to create dst bitmap");
								break;
							}
							
							TriglavPlugInPtr dstRaw = NULL;
							(*pBitmapService).getAddressProc(&dstRaw, dstBitmap, &zeroPos);
							TriglavPlugInInt dstRowBytes = 0;
							(*pBitmapService).getRowBytesProc(&dstRowBytes, dstBitmap);
							
							// Copy result to bitmap buffer (assuming RGBA structure match)
							for (TriglavPlugInInt y = 0; y < height; y++) {
								BYTE* dstRow = (BYTE*)dstRaw + (y * dstRowBytes);
								for (TriglavPlugInInt x = 0; x < width; x++) {
									size_t idx = (size_t)y * width + x;
									dstRow[x*4 + 0] = processor.resultRGB[idx*4 + 0];
									dstRow[x*4 + 1] = processor.resultRGB[idx*4 + 1];
									dstRow[x*4 + 2] = processor.resultRGB[idx*4 + 2];
									dstRow[x*4 + 3] = processor.resultRGB[idx*4 + 3];
								}
							}
							
							// 5. Write back to Dest Offscreen
							if((*pOffscreenService).setBitmapProc(destinationOffscreenObject, &srcPos, dstBitmap, &zeroPos, width, height, kTriglavPlugInOffscreenCopyModeNormal) != kTriglavPlugInAPIResultSuccess) {
								Log("Failed to write to dest offscreen");
							}

							TriglavPlugInFilterRunUpdateDestinationOffscreenRect(pRecordSuite, (*pluginServer).hostObject, &extent);

							// Cleanup
							srcBitmap.Release();
							dstBitmap.Release();
							
							Log("Loop Finished (one pass)");
							
							// End State
							TriglavPlugInInt processResult2;
							TriglavPlugInFilterRunProcess(pRecordSuite, &processResult2, (*pluginServer).hostObject, kTriglavPlugInFilterRunProcessStateEnd);
							
							if (processResult2 == kTriglavPlugInFilterRunProcessResultRestart) {
								restart = true;
							} else {
								break; 
							}
						}
					}
					
					// ScopeBitmap destructors handle leaks if loop breaks early

					Log("FilterRun Success");
					*result = kTriglavPlugInCallResultSuccess;
				}
			}
			catch (const std::exception& e) {
				Log("Exception caught: " + std::string(e.what()));
				*result = kTriglavPlugInCallResultFailed;
			}
			catch (...) {
				Log("Unknown exception caught in FilterRun");
				*result = kTriglavPlugInCallResultFailed;
			}
		}
	}
}
