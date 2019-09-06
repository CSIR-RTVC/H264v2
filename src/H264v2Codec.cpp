/** @file

MODULE        : H264v2Codec

TAG           : H264V2

FILE NAME     : H264v2Codec.cpp

DESCRIPTIONS  : A 2nd generation of video codecs based on the H264 standard
				        implementation. The primary interface is ICodecv2 for access
				        and configuration.

COPYRIGHT	    : (c)CSIR 2007-2019 all rights resevered

LICENSE		    : Software License Agreement (BSD License)

RESTRICTIONS  : Redistribution and use in source and binary forms, with or without
								modification, are permitted provided that the following conditions
								are met:

								* Redistributions of source code must retain the above copyright notice,
								this list of conditions and the following disclaimer.
								* Redistributions in binary form must reproduce the above copyright notice,
								this list of conditions and the following disclaimer in the documentation
								and/or other materials provided with the distribution.
								* Neither the name of the CSIR nor the names of its contributors may be used
								to endorse or promote products derived from this software without specific
								prior written permission.

								THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
								"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
								LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
								A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
								CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
								EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
								PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
								PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
								LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
								NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
								SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

===========================================================================
*/

#ifdef _WINDOWS
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#else
#include <stdio.h>
#endif

#include <cstdio>
#include <memory.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "H264v2Codec.h"

/// Macros
#include "CodecDistortionDef.h"

#include "RGBtoYUV420Converter.h"
#include "YUV420toRGBConverter.h"
#include "IBitStreamWriter.h"
#include "IBitStreamReader.h"
#include "OverlayMem2Dv2.h"
#include "IForwardTransform.h"
#include "IInverseTransform.h"
#include "IRunLengthCodec.h"
#include "VectorStructList.h"
#include "IMotionEstimator.h"
#include "IMotionCompensator.h"
#include "IMotionVectorPredictor.h"
#include "IVlcEncoder.h"
#include "IVlcDecoder.h"
#include "MacroBlockH264.h" 
#include "H264MbImgCache.h"
#include "IRateControl.h"

/// Implementations.
#include "BitStreamWriterMSB.h"
#include "BitStreamReaderMSB.h"

#include "RealRGB24toYUV420CCIR601ConverterVer16.h"
#include "RealYUV420toRGB24CCIR601ConverterVer16.h"	
#include "RealRGB24toYUV420ConverterImpl2Ver16.h"
#include "RealYUV420toRGB24ConverterImpl2Ver16.h"

#include "FastForward4x4ITImpl2.h"
#include "FastForward4x4ITImpl1.h"
#include "FastInverse4x4ITImpl1.h"
#include "FastForwardDC4x4ITImpl1.h"
#include "FastInverseDC4x4ITImpl1.h"
#include "FastForwardDC2x2ITImpl1.h"
#include "FastInverseDC2x2ITImpl1.h"
#include "FastForward4x4On16x16ITImpl1.h"
#include "FastInverse4x4On16x16ITImpl1.h"
#include "CAVLCH264Impl.h"
#include "CAVLCH264Impl2.h"

#include "MotionEstimatorH264ImplMultires.h"
#include "MotionEstimatorH264ImplMultiresCross.h"
#include "MotionEstimatorH264ImplCross.h"
#include "MotionEstimatorH264ImplMultiresCrossVer2.h"
#include "MotionEstimatorH264ImplFull.h"
#include "MotionEstimatorH264ImplUMHS.h"
#include "MotionEstimatorH264ImplFHS.h"
#include "MotionEstimatorH264ImplTest.h"
#include "MotionCompensatorH264ImplStd.h"
#include "H264MotionVectorPredictorImpl1.h"


#include "PrefixH264VlcEncoderImpl1.h"
#include "PrefixH264VlcDecoderImpl1.h"
#include "CoeffTokenH264VlcEncoder.h"
#include "CoeffTokenH264VlcDecoder.h"
#include "TotalZeros4x4H264VlcEncoder.h"
#include "TotalZeros4x4H264VlcDecoder.h"
#include "TotalZeros2x2H264VlcEncoder.h"
#include "TotalZeros2x2H264VlcDecoder.h"
#include "TotalZeros2x4H264VlcEncoder.h"
#include "TotalZeros2x4H264VlcDecoder.h"
#include "RunBeforeH264VlcEncoder.h"
#include "RunBeforeH264VlcDecoder.h"
#include "ExpGolombUnsignedVlcEncoder.h"
#include "ExpGolombUnsignedVlcDecoder.h"
#include "ExpGolombSignedVlcEncoder.h"
#include "ExpGolombSignedVlcDecoder.h"
#include "ExpGolombTruncVlcEncoder.h"
#include "ExpGolombTruncVlcDecoder.h"
#include "CodedBlkPatternH264VlcEncoder.h"
#include "CodedBlkPatternH264VlcDecoder.h"

#include "RateControlImplQuad.h"
#include "RateControlImplPow.h"
#include "RateControlImplLog.h"
//#include "RateControlImplMultiModel.h"  An incomplete work in progress.

/*
---------------------------------------------------------------------------
  Codec parameter constants.
---------------------------------------------------------------------------
*/
/// Picture type definitions. Values for _pictureCodingType member.
#define H264V2_INTRA				0
#define H264V2_INTER				1
#define H264V2_SEQ_PARAM		2
#define H264V2_PIC_PARAM		3

#define H264V2_I_PIC				0
#define H264V2_P_PIC				1
#define H264V2_PB_PIC				2
#define H264V2_B_PIC				3
#define H264V2_EI_PIC				4
#define H264V2_EP_PIC				5

#define H264V2_MAX_QP							  51	///< Maximum QP value
#define H264V2_MAX_EXT_QP	          71	///< Maximum extended QP value for Inter
#define H264V2_I_MAX_EXT_QP	        85	///< Maximum extended QP value for Intra

#define H264V2_MAX_INTRA_ITERATIONS	5 	///< Default settings for a limit on optimisation iterations for slow convergence.
#define H264V2_MAX_INTER_ITERATIONS	10 

/*
---------------------------------------------------------------------------
  Colour space constants.
---------------------------------------------------------------------------
*/
#define H264V2_RGB24							0		///< 888 (Default).
#define H264V2_RGB32							1		///< 8888.
#define H264V2_RGB16							2		///< 565.

#define H264V2_YUV420P16					16	///< Planar Y, U then V (16 bits/component).
#define H264V2_YUV420P8						17	///< Planar Y, U then V (8 bits/component).

/*
--------------------------------------------------------------------------
  Macros.
--------------------------------------------------------------------------
*/
#define H264V2_CLIP31(x)	( (((x) <= 31)&&((x) >= 1))? (x) : ( ((x) < 1)? 1:31 ) )
#define H264V2_CLIP51(x)	( (((x) <= 31)&&((x) >= 0))? (x) : ( ((x) < 0)? 0:51 ) )
#define H264V2_CLIP255(x)	( (((x) <= 255)&&((x) >= 0))? (x) : ( ((x) < 0)? 0:255 ) )

#undef	H264V2_EXACT_DMAX

#define H264V2_FAST_ABS32(x) ( ((x)^((x)>>31))-((x)>>31) )
#define H264V2_FAST_ABS16(x) ( ((x)^((x)>>15))-((x)>>15) )

/*
--------------------------------------------------------------------------
  Local constants.
--------------------------------------------------------------------------
*/
const int		H264v2Codec::PARAMETER_LEN = 37;
const char*	H264v2Codec::PARAMETER_LIST[] =
{
	"parameters",								            // 0
	"codecid",									            // 1
	"width",										            // 2
	"height",										            // 3
	"incolour",									            // 4
	"outcolour",								            // 5
  "flip",                                 // 6
	"picture coding type",			            // 7
	"last pic coding type",			            // 8
	"quality",									            // 9
	"autoipicture",							            // 10 
	"ipicturemultiplier",				            // 11
	"ipicturefraction",					            // 12
	"mode of operation",				            // 13
	"intra iteration limit",				        // 14
	"inter iteration limit",				        // 15
	"time limit msec",				              // 16
  "rate control model type",              // 17
	"seq param set",						            // 18
	"pic param set",						            // 19
	"gen param set on open",		            // 20
  "prepend param sets to i-pictures",     // 21
	"start code emulation prevention",      // 22
  "idr frame number",                     // 23
  "p frame number",                       // 24
  "seq param log2 max frame num minus 4", // 25
  "minimum intra qp",                     // 26
  "minimum intra qp",                     // 27
  "max distortion",                       // 28
  "num rate control frames",              // 29
  "max bits per frame",                   // 30
  "ipicture dmax multiplier",             // 31
  "ipicture dmax fraction",               // 32
  "rate overshoot percent",               // 33
  "enable roi encoding",                  // 34
  "motion estimation type",               // 35
  "motion resolution"                     // 36
};

const int		H264v2Codec::MEMBER_LEN = 7;
const char*	H264v2Codec::MEMBER_LIST[] =
{
	"members",									// 0
	"macroblocks",							// 1
  "reference",                // 2
	"autoiframedetectflag",			// 3
  "roi multiplier",			      // 4
  "currseqparamset",          // 5
  "currpicparamset"           // 6
};

/// Scaling is required for the DC coeffs to match the 4x4 
/// inverse IT scaling of the AC coeffs. Standard defines 
/// default Flat_4x4_16 for DC scaling.
const int H264v2Codec::dc4x4Scale[16] =
{ 16, 16, 16, 16,
	16, 16, 16, 16,
	16, 16, 16, 16,
	16, 16, 16, 16
};
const int H264v2Codec::dc2x2Scale[4] =
{ 16, 16,
	16, 16
};

/// Test sampling point coordinates for Intra_16x16 prediction mode selection on a grid defined by the method used in 
/// the Packet Video Conference Proceedings 2015 in a paper by K.T. Luhandjula and K.L. Ferguson entitled "Sampling 
/// Point Path Selection for Fast Intra Mode Prediction".
#define H264V2_16x16_PATH_LENGTH 20

const int H264v2Codec::testPntPath16xy[256][2] =
{
  {10,10},{7,5},{13,14},{3,12},{12,3},{5,8},{15,6},{7,13},{2,3},{10,1},{1,9},{12,9},{5,15},{6,2},{14,11},{9,7}, ///< 0-15
  {2,6},{14,2},{8,12},{4,4},{1,14},{11,6},{11,15},{5,0},{7,10},{0,11},{9,4},{15,10},{13,5},{4,7},{3,1},{10,13}, ///< 16-31
  {12,0},{4,11},{0,3},{8,8},{8,2},{3,14},{15,14},{6,7},{1,5},{12,12},{14,4},{3,9},{6,12},{13,7},{11,1},{0,7},   ///< 32-47
  {9,9},{5,4},{9,15},{2,10},{11,8},{4,0},{9,3},{1,12},{14,9},{7,1},{5,13},{15,1},{10,11},{0,2},{3,4},{13,15},   ///< 48-63
  {10,5},{7,14},{5,10},{12,2},{2,8},{13,11},{8,6},{0,14},{1,1},{15,8},{7,3},{9,0},{3,6},{11,12},{4,15},{14,3},  ///< 64-79
  {6,10},{14,13},{6,6},{10,7},{0,9},{4,2},{12,6},{8,14},{2,13},{2,0},{13,9},{9,8},{1,3},{6,1},{4,12},{6,4},     ///< 80-95
  {2,5},{11,11},{15,13},{1,11},{10,2},{1,4},{14,5},{8,11},{10,15},{3,8},{7,0},{0,6},{2,15},{15,0},{5,3},{14,8}, ///< 96-111
  {6,11},{12,14},{9,5},{7,7},{4,10},{9,13},{13,4},{11,9},{3,3},{4,6},{0,12},{5,1},{6,14},{14,12},{15,2},{9,10}, ///< 112-127
  {7,2},{1,7},{12,8},{4,9},{8,3},{3,13},{10,0},{12,5},{7,9},{15,7},{11,13},{7,15},{1,2},{5,6},{13,10},{11,3},   ///< 128-143
  {2,11},{15,15},{0,0},{1,15},{2,1},{10,6},{8,4},{5,12},{12,11},{11,0},{4,8},{9,8},{1,3},{6,1},{13,3},{13,13},  ///< 144-159
  {5,9},{9,12},{2,7},{15,4},{0,13},{9,14},{6,0},{3,5},{2,14},{13,6},{15,9},{8,5},{0,5},{11,10},{14,1},{3,7},    ///< 160-175
  {14,10},{8,13},{5,14},{3,2},{9,1},{1,10},{6,3},{11,7},{7,11},{12,15},{13,2},{2,9},{8,9},{10,4},{6,13},{4,1},  ///< 176-191
  {13,2},{2,9},{8,9},{10,4},{6,13},{4,1},{10,12},{0,8},{2,2},{3,11},{0,1},{10,8},{5,5},{14,7},{8,0},{0,10},     ///< 192-207
  {4,13},{6,5},{15,11},{11,2},{11,14},{8,7},{0,4},{8,10},{6,15},{13,12},{10,3},{7,4},{4,14},{1,8},{3,0},{12,7}, ///< 208-223
  {12,1},{5,11},{9,6},{4,3},{6,9},{4,5},{1,13},{15,12},{9,2},{10,14},{12,4},{1,0},{2,12},{10,9},{15,3},{5,2},   ///< 224-239
  {7,12},{14,14},{0,15},{13,8},{3,10},{14,0},{5,7},{9,11},{11,5},{1,6},{8,15},{8,1},{2,4},{12,13},{14,6},{6,8}  ///< 240-255
};
/*
const int H264v2Codec::testPntPath16xy[256][2] =
  {
	{0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{0,9},{0,10},{0,11},{0,12},{0,13},{0,14},{0,15}, ///< 0-15
	{1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},{1,7},{1,8},{1,9},{1,10},{1,11},{1,12},{1,13},{1,14},{1,15}, ///< 16-31
	{2,0},{2,1},{2,2},{2,3},{2,4},{2,5},{2,6},{2,7},{2,8},{2,9},{2,10},{2,11},{2,12},{2,13},{2,14},{2,15}, ///< 32-47
	{3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6},{3,7},{3,8},{3,9},{3,10},{3,11},{3,12},{3,13},{3,14},{3,15}, ///< 48-63
	{4,0},{4,1},{4,2},{4,3},{4,4},{4,5},{4,6},{4,7},{4,8},{4,9},{4,10},{4,11},{4,12},{4,13},{4,14},{4,15}, ///< 64-79
	{5,0},{5,1},{5,2},{5,3},{5,4},{5,5},{5,6},{5,7},{5,8},{5,9},{5,10},{5,11},{5,12},{5,13},{5,14},{5,15}, ///< 80-95
	{6,0},{6,1},{6,2},{6,3},{6,4},{6,5},{6,6},{6,7},{6,8},{6,9},{6,10},{6,11},{6,12},{6,13},{6,14},{6,15}, ///< 96-111
	{7,0},{7,1},{7,2},{7,3},{7,4},{7,5},{7,6},{7,7},{7,8},{7,9},{7,10},{7,11},{7,12},{7,13},{7,14},{7,15}, ///< 112-127
	{8,0},{8,1},{8,2},{8,3},{8,4},{8,5},{8,6},{8,7},{8,8},{8,9},{8,10},{8,11},{8,12},{8,13},{8,14},{8,15}, ///< 128-143
	{9,0},{9,1},{9,2},{9,3},{9,4},{9,5},{9,6},{9,7},{9,8},{9,9},{9,10},{9,11},{9,12},{9,13},{9,14},{9,15}, ///< 144-159
	{10,0},{10,1},{10,2},{10,3},{10,4},{10,5},{10,6},{10,7},{10,8},{10,9},{10,10},{10,11},{10,12},{10,13},{10,14},{10,15}, ///< 160-175
	{11,0},{11,1},{11,2},{11,3},{11,4},{11,5},{11,6},{11,7},{11,8},{11,9},{11,10},{11,11},{11,12},{11,13},{11,14},{11,15}, ///< 176-191
	{12,0},{12,1},{12,2},{12,3},{12,4},{12,5},{12,6},{12,7},{12,8},{12,9},{12,10},{12,11},{12,12},{12,13},{12,14},{12,15}, ///< 192-207
	{13,0},{13,1},{13,2},{13,3},{13,4},{13,5},{13,6},{13,7},{13,8},{13,9},{13,10},{13,11},{13,12},{13,13},{13,14},{13,15}, ///< 208-223
	{14,0},{14,1},{14,2},{14,3},{14,4},{14,5},{14,6},{14,7},{14,8},{14,9},{14,10},{14,11},{14,12},{14,13},{14,14},{14,15}, ///< 224-239
	{15,0},{15,1},{15,2},{15,3},{15,4},{15,5},{15,6},{15,7},{15,8},{15,9},{15,10},{15,11},{15,12},{15,13},{15,14},{15,15}  ///< 240-255
  };
*/

#define H264V2_8x8_PATH_LENGTH 5

const int H264v2Codec::testPntPath8xy[64][2] =
{
  {5,5},{3,3},{7,7},{2,6},{6,1},{2,4},{7,3},{3,6},    ///< 0-7
  {1,2},{5,0},{0,4},{6,4},{2,7},{3,1},{7,5},{4,3},    ///< 8-15
  {1,3},{7,1},{4,6},{2,2},{0,7},{5,3},{5,7},{2,0},    ///< 16-23
  {3,5},{0,5},{4,2},{7,4},{6,2},{2,3},{1,0},{5,6},    ///< 24-31
  {6,0},{0,1},{4,4},{4,1},{1,7},{6,6},{3,4},{0,2},    ///< 32-39
  {6,5},{7,2},{1,4},{3,7},{6,3},{5,1},{0,3},{4,5},    ///< 40-47
  {3,2},{4,7},{1,5},{5,4},{2,1},{5,2},{0,6},{7,6},    ///< 48-55
  {3,0},{2,5},{7,0},{6,7},{1,1},{0,0},{4,0},{1,6}     ///< 56-63
};

/// Test sampling point coordinates for Intra_16x16 prediction mode selection on a spread out grid.
const int H264v2Codec::test16X[256] = { 3, 11,  3, 11,
									1,  5,  9, 13,  1,  5,  9, 13,  1,  5,  9, 13,  1,  5,  9, 13,
									3,  7, 11, 15,  0,  7, 15,  0,  3,  7, 11, 15,  0,  7, 15,  0,  3,  7, 11, 15,
									0,  2,  4,  6,  8, 10, 12, 14,  2,  4,  6,  8, 10, 12, 14,  2,  4,  6,  8, 10, 12, 14,  2,  4,  6,  8, 10, 12, 14,
									5,  9, 13,  1,  5,  9, 13,  2,  4,  6,  8, 10, 12, 14,  0,  3,  7, 11, 15,  1,  5,  9, 13,  2,  4,  6,  8, 10, 12, 14,  0,  3,  7, 11, 15,  1,  5,  9, 13,  2,  4,  6,  8, 10, 12, 14,  0,  3,  7, 11, 15,  5,  9, 13,
									1,  2,  4,  6,  8, 10, 12, 14,  0,  3,  7, 11, 15,  0,  3, 11, 15,  6, 14,  0,  1,  9, 15,  0,  3, 11, 15,  0,  3, 11, 15,  0,  1, 15,  6, 14,  0,  9, 15,  0,  3, 11, 15,  1,  2,  4,  6,  8, 10, 12, 14,
									2,  4,  6,  8, 10, 12, 14,  2,  4,  8, 10, 12,  2,  4,  6,  8, 10, 12, 14,  2,  4,  6,  8, 10, 12, 14,  2,  4,  6,  8, 10, 12, 14,  2,  4,  8, 10, 12,  2,  4,  6,  8, 10, 12, 14,
									1,  5,  7,  9, 13,  3,  5,  7, 11, 13,  1,  5,  7,  9, 13,  1,  5,  7,  9, 13,  3,  5,  7,  9, 11, 13,  1,  3,  5,  7, 11, 13,  1,  5,  7,  9, 13 };
const int H264v2Codec::test16Y[256] = { 3,  3, 11, 11,
									1,  1,  1,  1,  5,  5,  5,  5,  9,  9,  9,  9, 13, 13, 13, 13,
									0,  0,  0,  0,  3,  3,  3,  7,  7,  7,  7,  7, 11, 11, 11, 15, 15, 15, 15, 15,
									0,  2,  2,  2,  2,  2,  2,  2,  6,  6,  6,  6,  6,  6,  6, 10, 10, 10, 10, 10, 10, 10, 14, 14, 14, 14, 14, 14, 14,
									0,  0,  0,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 15, 15, 15,
									0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  4,  4,  4,  4,  6,  6,  6,  6,  8,  8,  8,  8, 10, 10, 10, 11, 11, 12, 12, 12, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15,
									1,  1,  1,  1,  1,  1,  1,  3,  3,  3,  3,  3,  5,  5,  5,  5,  5,  5,  5,  7,  7,  7,  7,  7,  7,  7,  9,  9,  9,  9,  9,  9,  9, 11, 11, 11, 11, 11, 13, 13, 13, 13, 13, 13, 13,
									2,  2,  2,  2,  2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  6,  8,  8,  8,  8,  8, 10, 10, 10, 10, 10, 10, 12, 12, 12, 12, 12, 12, 14, 14, 14, 14, 14 };
const int H264v2Codec::test16Limit[8] = { 4, 20, 40, 69, 123, 174, 219, 256 };

/// Test sampling point coordinates for Intra_16x16 prediction mode selection on a zooming square grid to be 
/// used 4 at a time where {(0,0), (0,8), (8,0), (8,8)} is added to each address in the table.
const int H264v2Codec::testZoom16X[21] =
{ 3,
  1, 5, 1, 5,
  0, 4, 0, 4,
  2, 6, 2, 6,
  0, 4, 0, 4,
  2, 6, 2, 6
};
const int H264v2Codec::testZoom16Y[21] =
{ 3,
  1, 1, 5, 5,
  0, 0, 4, 4,
  0, 0, 4, 4,
  2, 2, 6, 6,
  2, 2, 6, 6
};
const int H264v2Codec::testZoom16Len = 21;
const int H264v2Codec::delta16[4][2] = { {0,0}, {8,0}, {0,8}, {8,8} };
///< Thresholds for predicted final distortion = 20480/(256/4) = 320 (4 samples/mode completed)
#define H264V2_LUM_DISTORTION_THRESHOLD 320

const int H264v2Codec::testZoom8[5][2] = { {3,3}, {1,1}, {5,1}, {1,5}, {5,5} };
const int H264v2Codec::testZoom8Len = 5;
///< Thresholds for predicted final distortion = 10240/(128/2) = 160 (2 samples/mode completed)
#define H264V2_CHR_DISTORTION_THRESHOLD 160

/// Test sampling point coordinates for Intra_8x8 prediction mode selection.
const int H264v2Codec::test8X[64] = { 2, 5, 2, 5,
																			0, 4, 7, 0, 7, 0, 4, 7,
																			2, 5, 6, 1, 6, 0, 7, 3, 4, 3, 4, 0, 7, 1, 6, 2, 5,
																			1, 3, 0, 2, 3, 4, 5, 7, 1, 3, 4, 6, 0, 1, 2, 5, 6, 7, 1, 2, 5, 6, 1, 3, 4, 6, 0, 2, 3, 4, 5, 7, 1, 3, 6 };
const int H264v2Codec::test8Y[64] = { 2, 2, 5, 5,
																			0, 0, 0, 4, 4, 7, 7, 7,
																			0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
																			0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7 };
const int H264v2Codec::test8Limit[4] = { 4, 12, 29, 64 };

/// Loop filter constants.
const int H264v2Codec::alpha[52] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 5, 6, 7, 8, 9, 10, 12, 13, 15, 17, 20, 22, 25, 28, 32, 36, 40, 45, 50, 56, 63, 71, 80, 90, 101, 113, 127, 144, 162, 182, 203, 226, 255, 255 };
const int H264v2Codec::beta[52] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 3, 3, 3, 3,  4,  4,  4,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18 };
///                                           0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51
const int H264v2Codec::indexAbS[3][52] = { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9,10,11,13 },
																						{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8,10,11,12,13,15,17 },
																						{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9,10,11,13,14,16,18,20,23,25} };

/// Fast search on the macroblock distortion vs. QP curve changes the step size
/// depending on the QP value.
#ifdef H264V2_EXACT_DMAX
const int H264v2Codec::MbStepSize[H264V2_MAX_QP + 1] =
{ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
#else
const int H264v2Codec::MbStepSize[H264V2_MAX_EXT_QP + 1] =
{//	0		1		2		3		4		5		6		7		8		9		10	11	12	13	14	15
		1,	1,	1,	1,	1,	1,	1,	1,	1,	1,	 1,	 1,	 1,	 2,	 2,	 2,
//	16	17	18	19	20	21	22	23	24	25	26	27	28	29	30	31
		2,	 2,	 2,	 2,	 2,	 2,	 2,	 2,	 2,	 3,	 3,	 3,	 3,	 3,	 3,	 3,
//	32	33	34	35	36	37	38	39	40	41	42	43	44	45	46	47
		3,	 3,	 3,	 3,	 3,	 4,	 4,	 4,	 4,	 4,	 4,	 4,	 4,	 4,	 4,	 4,
//	48	49	50	51  52  53  54  55  56  57  58  59  60  61  62  63  	
		4,	 4,	 4,	 4,  1,  2,  3,  4,  1,  2,  3,  4,  1,  2,  3,  4,
//  64  65  66  67  68  69  70  71
    1,   1,  1,  1,  1,  1,  1,  1
};
#endif
const int H264v2Codec::NextQPDec[H264V2_MAX_EXT_QP + 1] =
{//	0		1		2		3		4		5		6		7		8		9		10	11	12	13	14	15
		1,	1,	1,	2,	3,	4,	5,	6,	7,	8,	 9,	10,	11,	11,	13,	13,
//	16	17	18	19	20	21	22	23	24	25	26	27	28	29	30	31
		15,	15,	17,	17,	19,	19,	21,	21,	23,	23,	25,	25,	27,	27,	29,	29,
//	32	33	34	35	36	37	38	39	40	41	42	43	44	45	46	47
		31,	31,	31,	31,	35,	35,	35,	35,	39,	39,	39,	39,	43,	43,	43,	43,
//	48	49	50	51  52  53  54  55  56  57  58  59  60  61  62  63  	
		47,	47,	47,	47, 51, 51, 51, 51, 55, 55, 55, 55, 59, 59, 59, 59,
//  64  65  66  67  68  69  70  71
		63, 63, 63, 63, 67, 68, 69, 70
};

/// CPU speed dependent high resolution timer.
double H264v2Codec::_cpuFreq = 0.0;

/// Motion lambda constants for TCP-like adaptation.
const double H264v2Codec::MVLAMBDA_MAX          = 4.0;
const double H264v2Codec::MVLAMBDA_STEP         = -0.1;
const double H264v2Codec::MVLAMBDA_MULT         = 0.5;
const double H264v2Codec::MVLAMBDA_STEADYSTATE  = 0.9;

/*
--------------------------------------------------------------------------
  Construction and destruction.
--------------------------------------------------------------------------
*/

H264v2Codec::H264v2Codec()
{
	ResetMembers();
}//end constructor.

void H264v2Codec::ResetMembers(void)
{
#ifdef H264V2_DUMP_HEADERS
	_headerTableLen = 300;
	_headerTable.Create(6, _headerTableLen);

	_headerTable.SetHeading(0, "NALType");
	_headerTable.SetHeading(1, "NALRefIdc");
	_headerTable.SetHeading(2, "IdrPicId");
	_headerTable.SetHeading(3, "FrmNum");
	_headerTable.SetHeading(4, "SliceType");
	_headerTable.SetHeading(5, "Qp");

	for (int j = 0; j < 6; j++)
		_headerTable.SetDataType(j, MeasurementTable::INT);

	_headerTablePos = 0;
#endif /// H264V2_DUMP_HEADERS
#ifdef H264V2_DUMP_MB_RD_DATA
	int j;
	/// Collect macroblock R or D values for each QP for 64 macroblocks.
	_mbRDTableLen = 53;
	_mbRDTable.Create(65, _mbRDTableLen);  /// 1 for QP and 1 D value per mb

	_mbRDTable.SetHeading(0, "QP");
	_mbRDTable.SetHeading(1, "Distortion");
	for (j = 2; j < 65; j++)
		_mbRDTable.SetHeading(j, " ");

	for (j = 0; j < 65; j++)
		_mbRDTable.SetDataType(j, MeasurementTable::INT);

	/// 1st column is QP value for all mbs.
	_mbRDTable.WriteItem(0, 0, 0);
	for (j = 1; j < _mbRDTableLen; j++)
		_mbRDTable.WriteItem(0, j, j);

	_mbRDTablePos = 1;
#endif /// H264V2_DUMP_MB_RD_DATA
#ifdef H264V2_DUMP_TIMING
	_timingTableLen = 300;
	_timingTable.Create(2, _timingTableLen);

	_timingTable.SetHeading(0, "Frame");
	_timingTable.SetHeading(1, "Time (ms)");

	_timingTable.SetDataType(0, MeasurementTable::INT);
	_timingTable.SetDataType(1, MeasurementTable::INT);

	_timingTablePos = 0;
#endif /// H264V2_DUMP_TIMING

	_errorStr = "[H264v2Codec::ResetMembers] No error";
	_codecIsOpen = 0;
	_bitStreamSize = 0;

	/// Set default parameter values.
	_idCode = (int)H264V2_ID;
	_width = 176;
	_height = 144;
	_inColour = H264V2_RGB24;	///< Default to 24 bit rgb.
	_outColour = H264V2_RGB24;
	_flip = false;
	_pictureCodingType = H264V2_INTRA;
	_lastPicCodingType = H264V2_INTRA;
	_pQuant = 26;		///< Mid range [1..51].
	_minQPIntra = 16;   ///< Min QP value during mb optimisation
	_minQPInter = 4;
	_autoIPicture = 1;
	_iPictureMultiplier = 1;
	_iPictureFraction = 0;
	_iPictureDMaxMultiplier = 2;
	_iPictureDMaxFraction = 0;
	_dMax = 30000;               ///< Max distortion for all mbs with modeOfOperation = H264V2_DMAX.
	_maxBitsPerFrame = 100240;   ///< This defines the max bit limit for modeOfOperation = H264V2_MINMAX_RATECNT or H264V2_MINAVG_RATECNT.
	_numRateCntlFrames = 16;     ///< Num of frames to do avg rate adaption over with  modeOfOperation = H264V2_MINMAX_RATECNT or H264V2_MINAVG_RATECNT.
	_rateOvershootPercent = 100; ///< Percentage that the avg rate can be exceeded from frame to frame. 
	_coeffBitsPerPel = 0.0001;

	_modeOfOperation = H264V2_FIXED_QP;
	_intraIterations = H264V2_MAX_INTRA_ITERATIONS;    ///< Intra optimisation iteration limit.
	_interIterations = H264V2_MAX_INTER_ITERATIONS;    ///< Inter optimisation iteration limit.
	_timeLimitMs = 0;  ///< Intra/inter optimisation time limit.
	_rateControlModelType = H264V2_RATE_CONTROL_MODEL_LOG;  ///< For mode of operation = H264V2_MINMAX_RATECNT or H264V2_MINAVG_RATECNT.

  _motionEstimationType = H264V2_MOTION_CROSS_PARTIAL;
  _motionResolution     = H264V2_MOTION_RES_QUARTER;

	_currSeqParam = 0;	///< Index reference into _seqParam[32] array.
	_currPicParam = 0;	///< Index reference into _picParam[2] array.
	_genParamSetOnOpen = 1;	///< Generate the seq/pic parameter sets on the Open() call.
	_prependParamSetsToIPic = 1;  ///< Prepend a SPS and PPS NAL unit to the front of every I-Picture.

	_startCodeEmulationPrevention = 1;  ///< Enable/disable start code emulation prevention in bit stream.

	/// Work input image.
	_lumWidth = 0;
	_lumHeight = 0;
	_pLum = NULL;
	_chrWidth = 0;
	_chrHeight = 0;
	_pChrU = NULL;
	_pChrV = NULL;
	_Lum = NULL;
	_Cb = NULL;
	_Cr = NULL;
	/// Reference images.
	_pRLum = NULL;
	_pRChrU = NULL;
	_pRChrV = NULL;
	_RefLum = NULL;
	_RefCb = NULL;
	_RefCr = NULL;

	/// Temp work mem.
	_p16x16 = NULL;
	_16x16 = NULL;
	_p8x8_0 = NULL;
	_8x8_0 = NULL;
	_p8x8_1 = NULL;
	_8x8_1 = NULL;

	/// Cache
	_mbImg = NULL;

	/// Colour converters.
	_pInColourConverter = NULL;
	_pOutColourConverter = NULL;
	/// Stream access.
	_pBitStreamWriter = NULL;
	_pBitStreamReader = NULL;
	/// IT transform filters.
	_pF4x4TLum = NULL;
	_pF4x4TChr = NULL;
	_pFDC4x4T = NULL;
	_pFDC2x2T = NULL;
	_pI4x4TLum = NULL;
	_pI4x4TChr = NULL;
	_pIDC4x4T = NULL;
	_pIDC2x2T = NULL;

	/// For motion estimation (and compensation).
	_motionFactor             = 2; ///< Default for abs diff algorithms.
	_prevMotionDistortion     = -1;
	_autoIFrameIncluded       = NULL;
	_pMotionEstimator         = NULL;
	_pMotionEstimationResult  = NULL;
	_pMotionCompensator       = NULL;
	_pMotionVectors           = NULL;
	_pMotionPredictor         = NULL;

	/// Vlc encoders and decoders for use with CAVLC.
	_pPrefixVlcEnc = NULL;
	_pPrefixVlcDec = NULL;
	_pCoeffTokenVlcEnc = NULL;
	_pCoeffTokenVlcDec = NULL;
	_pTotalZeros4x4VlcEnc = NULL;
	_pTotalZeros4x4VlcDec = NULL;
	_pTotalZeros2x2VlcEnc = NULL;
	_pTotalZeros2x2VlcDec = NULL;
	_pRunBeforeVlcEnc = NULL;
	_pRunBeforeVlcDec = NULL;
	/// ... and the remaining vlc encoders and decoders.
	_pBlkPattVlcEnc = NULL;
	_pBlkPattVlcDec = NULL;
	_pDeltaQPVlcEnc = NULL;
	_pDeltaQPVlcDec = NULL;
	_pMbTypeVlcEnc = NULL;
	_pMbTypeVlcDec = NULL;
	_pMbIChrPredModeVlcEnc = NULL;
	_pMbIChrPredModeVlcDec = NULL;
	_pMbMotionVecDiffVlcEnc = NULL;
	_pMbMotionVecDiffVlcDec = NULL;
	/// The CAVLC codecs.
	_pCAVLC4x4 = NULL;
	_pCAVLC2x2 = NULL;
	/// General header vlc encoders and decoders.
	_pHeaderUnsignedVlcEnc = NULL;
	_pHeaderUnsignedVlcDec = NULL;
	_pHeaderSignedVlcEnc = NULL;
	_pHeaderSignedVlcDec = NULL;

	/// Macroblock data objects.
	_pMb = NULL;
	_Mb = NULL;

	/// Internal parameters.

	  /// H.264 has inter prediction mechanisms that permit multiple refrence pictures/frames and
	  /// control parameters to maintain a list of these references. However, in this implementation
	  /// the GOP are IPPPPPP..... and all P-frames use only the previous frame for reference. No
	  /// reference lists (List0) are maintained and frame number counting is consecutively incremented
	  /// from the previous IDR-frame.
	_seqParamSetLog2MaxFrameNumMinus4 = 12;         ///< Range [0..12] made available as a codec parameter.
	_frameNum = 0;				  ///< Used for SliceHeader._frame_num
	_maxFrameNum = 1 << (_seqParamSetLog2MaxFrameNumMinus4 + 4);	///< Used for and derived from SeqParamSet._log2_max_frame_num_minus4
	_idrFrameNum = 0;				  ///< Used for SliceHeader._idr_pic_id

	/// Slice without partitioning and therefore the slice parameters are simple. (Only one slice/frame.)
	_slice._type = SliceHeaderH264::I_Slice_All;	///< I-Slice/P-Slice.
	_slice._qp = _pQuant;	                    ///< Same as _pQuant for this implementation.
	_slice._qp_delta = 0;				                    ///< = 0 for this implementation.
	_slice._disable_deblocking_filter_idc = 0;
	_mb_skip_run = 0;				                    ///< For P-Slices a skip run preceeds each macroblock.

	/// Image plane encoders/decoders.
	_pIntraImgPlaneEncoder = NULL;
	_pInterImgPlaneEncoder = NULL;
	_pIntraImgPlaneDecoder = NULL;
	_pInterImgPlaneDecoder = NULL;

	/// Rate controllers.
	_pRateCntlPFrames = NULL;
	_pRateCntlIFrames = NULL;
  _frameDistortion = 0;

  /// Region of Interest encoding.
  _roiMultiplier      = NULL;
  _enableROIEncoding  = 0; ///< Default is off.

	/// Motion vector Le Grange multiplier adaptation.
	_mvLambda = MVLAMBDA_STEADYSTATE;

	/// Timer
	_startTime = 0;

}//end ResetMembers.

H264v2Codec::~H264v2Codec()
{
	// If open then close first before exiting.
	if (_codecIsOpen)
		Close();
}//end destructor.

/*
-----------------------------------------------------------------------
  Parameter interface functions.
-----------------------------------------------------------------------
*/
int	H264v2Codec::GetParameter(const char* type, int*	length, void*	value)
{

	char *p = (char *)type;
	int len = (int)strlen(p);

	/// Most frequently called params at the top of the comparison list.
	if (strncmp(p, "picture coding type", len) == 0)
		//_itoa(_pictureCodingType,(char *)value,10);
		sprintf((char *)value, "%d", _pictureCodingType);
	else if (strncmp(p, "last pic coding type", len) == 0)
		//_itoa(_lastPicCodingType,(char *)value,10);
		sprintf((char *)value, "%d", _lastPicCodingType);
	else if (strncmp(p, "idr frame number", len) == 0)
		//_itoa(_idrFrameNum,(char *)value,10);
		sprintf((char *)value, "%d", _idrFrameNum);
	else if (strncmp(p, "p frame number", len) == 0)
		//_itoa(_frameNum,(char *)value,10);
		sprintf((char *)value, "%d", _frameNum);
	else if (strncmp(p, "width", len) == 0)
		//_itoa(_width,(char *)value,10);
		sprintf((char *)value, "%d", _width);
	else if (strncmp(p, "height", len) == 0)
		//_itoa(_height,(char *)value,10);
		sprintf((char *)value, "%d", _height);
	else if (strncmp(p, "incolour", len) == 0)
		//_itoa(_inColour,(char *)value,10);
		sprintf((char *)value, "%d", _inColour);
	else if (strncmp(p, "outcolour", len) == 0)
		//_itoa(_outColour,(char *)value,10);
		sprintf((char *)value, "%d", _outColour);
	else if (strncmp(p, "flip", len) == 0)
	{
		int i = _flip ? 1 : 0;
		sprintf((char *)value, "%d", i);
	}
	else if (strncmp(p, "quality", len) == 0)
		//_itoa(_pQuant,(char *)value,10);
		sprintf((char *)value, "%d", _pQuant);
	else if (strncmp(p, "autoipicture", len) == 0)
		//_itoa(_autoIPicture,(char *)value,10);
		sprintf((char *)value, "%d", _autoIPicture);
	else if (strncmp(p, "ipicturemultiplier", len) == 0)
		//_itoa(_iPictureMultiplier,(char *)value,10);
		sprintf((char *)value, "%d", _iPictureMultiplier);
	else if (strncmp(p, "ipicturefraction", len) == 0)
		//_itoa(_iPictureFraction,(char *)value,10);
		sprintf((char *)value, "%d", _iPictureFraction);
	else if (strncmp(p, "codecid", len) == 0)
		//_itoa(_idCode,(char *)value,10);
		sprintf((char *)value, "%d", _idCode);
	else if (strncmp(p, "mode of operation", len) == 0)
		//_itoa(_modeOfOperation,(char *)value,10);
		sprintf((char *)value, "%d", _modeOfOperation);
	else if (strncmp(p, "intra iteration limit", len) == 0)
		//_itoa(_intraIterations,(char *)value,10);
		sprintf((char *)value, "%d", _intraIterations);
	else if (strncmp(p, "inter iteration limit", len) == 0)
		//_itoa(_interIterations,(char *)value,10);
		sprintf((char *)value, "%d", _interIterations);
	else if (strncmp(p, "time limit msec", len) == 0)
		//_itoa(_timeLimitMs,(char *)value,10);
		sprintf((char *)value, "%d", _timeLimitMs);
	else if (strncmp(p, "rate control model type", len) == 0)
		//_itoa(_rateControlModelType,(char *)value,10);
		sprintf((char *)value, "%d", _rateControlModelType);
	else if (strncmp(p, "seq param set", len) == 0)
		//_itoa(_currSeqParam,(char *)value,10);
		sprintf((char *)value, "%d", _currSeqParam);
	else if (strncmp(p, "pic param set", len) == 0)
		//_itoa(_currPicParam,(char *)value,10);
		sprintf((char *)value, "%d", _currPicParam);
	else if (strncmp(p, "generate param set on open", len) == 0)
		//_itoa(_genParamSetOnOpen,(char *)value,10);
		sprintf((char *)value, "%d", _genParamSetOnOpen);
	else if (strncmp(p, "prepend param sets to i-pictures", len) == 0)
		//_itoa(_prependParamSetsToIPic,(char *)value,10);
		sprintf((char *)value, "%d", _prependParamSetsToIPic);
	else if (strncmp(p, "start code emulation prevention", len) == 0)
		//_itoa(_startCodeEmulationPrevention,(char *)value,10);
		sprintf((char *)value, "%d", _startCodeEmulationPrevention);
	else if (strncmp(p, "seq param log2 max frame num minus 4", len) == 0)
		//_itoa(_seqParamSetLog2MaxFrameNumMinus4,(char *)value,10);
		sprintf((char *)value, "%d", _seqParamSetLog2MaxFrameNumMinus4);
	else if (strncmp(p, "minimum intra qp", len) == 0)
		//_itoa(_minQPIntra,(char *)value,10);
		sprintf((char *)value, "%d", _minQPIntra);
	else if (strncmp(p, "minimum inter qp", len) == 0)
		//_itoa(_minQPInter,(char *)value,10);
		sprintf((char *)value, "%d", _minQPInter);
	else if (strncmp(p, "max distortion", len) == 0)
		//_itoa(_dMax,(char *)value,10);
		sprintf((char *)value, "%d", _dMax);
	else if (strncmp(p, "num rate control frames", len) == 0)
		//_itoa(_numRateCntlFrames,(char *)value,10);
		sprintf((char *)value, "%d", _numRateCntlFrames);
	else if (strncmp(p, "max bits per frame", len) == 0)
		//_itoa(_maxBitsPerFrame,(char *)value,10);
		sprintf((char *)value, "%d", _maxBitsPerFrame);
	else if (strncmp(p, "ipicture dmax multiplier", len) == 0)
		//_itoa(_iPictureDMaxMultiplier,(char *)value,10);
		sprintf((char *)value, "%d", _iPictureDMaxMultiplier);
	else if (strncmp(p, "ipicture dmax fraction", len) == 0)
		//_itoa(_iPictureDMaxFraction,(char *)value,10);
		sprintf((char *)value, "%d", _iPictureDMaxFraction);
	else if (strncmp(p, "rate overshoot percent", len) == 0)
		//_itoa(_rateOvershootPercent,(char *)value,10);
		sprintf((char *)value, "%d", _rateOvershootPercent);
  else if (strncmp(p, "enable roi encoding", len) == 0)
    sprintf((char *)value, "%d", _enableROIEncoding);
  else if (strncmp(p, "motion estimation type", len) == 0)
    sprintf((char *)value, "%d", _motionEstimationType);
  else if (strncmp(p, "motion resolution", len) == 0)
    sprintf((char *)value, "%d", _motionResolution);
  else if (strncmp(p, "parameters", len) == 0)
		//_itoa(PARAMETER_LEN,(char *)value,10);
		sprintf((char *)value, "%d", PARAMETER_LEN);
	else
	{
		_errorStr = "[H264v2Codec::GetParameter] Read parameter not supported";
		return(0);
	}//end else...

	*length = (int)strlen((char *)value);
	return(1);

	return(0);
}//end GetParameter.

void H264v2Codec::GetParameterName(int ordinal, const char** name, int* length)
{

	if ((ordinal < 0) || (ordinal >= PARAMETER_LEN))
		return;
	*name = (const char *)PARAMETER_LIST[ordinal];
	*length = (int)strlen(PARAMETER_LIST[ordinal]);

}//end GetParameterName.

int H264v2Codec::SetParameter(const char* type, const char* value)
{

	char *p = (char *)type;
	char *v = (char *)value;
	int  len = (int)strlen(p);

	/// Most frequently called params at the top of the comparison list.
	if (strncmp(p, "picture coding type", len) == 0)
		_pictureCodingType = (int)(atoi(v));
	else if (strncmp(p, "idr frame number", len) == 0)
		_idrFrameNum = (int)(atoi(v));
	else if (strncmp(p, "p frame number", len) == 0)
		_frameNum = (int)(atoi(v));
	else if (strncmp(p, "width", len) == 0)
		_width = (int)(atoi(v));
	else if (strncmp(p, "height", len) == 0)
		_height = (int)(atoi(v));
	else if (strncmp(p, "incolour", len) == 0)
		_inColour = (int)(atoi(v));
	else if (strncmp(p, "outcolour", len) == 0)
		_outColour = (int)(atoi(v));
	else if (strncmp(p, "flip", len) == 0)
	{
		int i = (atoi)(v);
		_flip = (i != 0);
	}
	else if (strncmp(p, "quality", len) == 0)
		_pQuant = (int)(atoi(v));
	else if (strncmp(p, "autoipicture", len) == 0)
		_autoIPicture = (int)(atoi(v));
	else if (strncmp(p, "ipicturemultiplier", len) == 0)
		_iPictureMultiplier = (int)(atoi(v));
	else if (strncmp(p, "ipicturefraction", len) == 0)
		_iPictureFraction = (int)(atoi(v));
	else if (strncmp(p, "mode of operation", len) == 0)
		_modeOfOperation = (int)(atoi(v));
	else if (strncmp(p, "intra iteration limit", len) == 0)
		_intraIterations = (int)(atoi(v));
	else if (strncmp(p, "inter iteration limit", len) == 0)
		_interIterations = (int)(atoi(v));
	else if (strncmp(p, "time limit msec", len) == 0)
		_timeLimitMs = (int)(atoi(v));
	else if (strncmp(p, "rate control model type", len) == 0)
		_rateControlModelType = (int)(atoi(v));
	else if (strncmp(p, "seq param set", len) == 0)
		_currSeqParam = (int)(atoi(v));
	else if (strncmp(p, "pic param set", len) == 0)
		_currPicParam = (int)(atoi(v));
	else if (strncmp(p, "generate param set on open", len) == 0)
		_genParamSetOnOpen = (int)(atoi(v));
	else if (strncmp(p, "prepend param sets to i-pictures", len) == 0)
		_prependParamSetsToIPic = (int)(atoi(v));
	else if (strncmp(p, "start code emulation prevention", len) == 0)
		_startCodeEmulationPrevention = (int)(atoi(v));
	else if (strncmp(p, "seq param log2 max frame num minus 4", len) == 0)
		_seqParamSetLog2MaxFrameNumMinus4 = (int)(atoi(v));
	else if (strncmp(p, "minimum intra qp", len) == 0)
		_minQPIntra = (int)(atoi(v));
	else if (strncmp(p, "minimum inter qp", len) == 0)
		_minQPInter = (int)(atoi(v));
	else if (strncmp(p, "max distortion", len) == 0)
		_dMax = (int)(atoi(v));
	else if (strncmp(p, "num rate control frames", len) == 0)
		_numRateCntlFrames = (int)(atoi(v));
	else if (strncmp(p, "max bits per frame", len) == 0)
		_maxBitsPerFrame = (int)(atoi(v));
	else if (strncmp(p, "ipicture dmax multiplier", len) == 0)
		_iPictureDMaxMultiplier = (int)(atoi(v));
	else if (strncmp(p, "ipicture dmax fraction", len) == 0)
		_iPictureDMaxFraction = (int)(atoi(v));
	else if (strncmp(p, "rate overshoot percent", len) == 0)
		_rateOvershootPercent = (int)(atoi(v));
  else if (strncmp(p, "enable roi encoding", len) == 0)
    _enableROIEncoding = (int)(atoi(v));
  else if (strncmp(p, "motion estimation type", len) == 0)
    _motionEstimationType = (int)(atoi(v));
  else if (strncmp(p, "motion resolution", len) == 0)
    _motionResolution = (int)(atoi(v));
  else
	{
		_errorStr = "[H264v2Codec::SetParameter] Write parameter not supported";
		return(0);
	}//end else...

	return(1);
}//end SetParameter.

/**
-----------------------------------------------------------------------
  Member interface functions.
-----------------------------------------------------------------------
*/
void* H264v2Codec::GetMember(const char* type, int* length)
{
	char*	p = (char *)type;
	int		len = (int)strlen(p);
	void* pRet = NULL;
	*length = 0;

	if (strncmp(p, "macroblocks", len) == 0)
	{
		*length = _mbLength;
		pRet = (void *)_pMb;
	}
	else if (strncmp(p, "autoiframedetectflag", len) == 0)
	{
		*length = _mbLength;
		pRet = (void *)_autoIFrameIncluded;
	}
  else if (strncmp(p, "roi multiplier", len) == 0)
  {
    *length = _mbLength;
    pRet = (void *)_roiMultiplier;
  }
  else if (strncmp(p, "reference", len) == 0)
	{
		*length = (_lumWidth * _lumHeight) + 2 * (_chrWidth * _chrHeight);
		pRet = (void *)_pRLum;
	}
	else if (strncmp(p, "currseqparamset", len) == 0)
	{
		*length = 1;
		pRet = (void *)(&(_seqParam[_currSeqParam]));
	}
	else if (strncmp(p, "currpicparamset", len) == 0)
	{
		*length = 1;
		pRet = (void *)(&(_picParam[_currPicParam]));
	}
	else if (strncmp(p, "members", len) == 0)
	{
		int numMembers = (int)MEMBER_LEN;
		*length = 1;
		pRet = (void *)(numMembers);	///< Note that MEMBER_LEN is converted to a pointer type.
	}//end else if members...
	else
		_errorStr = "[H264v2Codec::GetMember] Read member not supported";

	return(pRet);
}//end GetMember.

void H264v2Codec::GetMemberName(int ordinal, const char** name, int* length)
{

	if ((ordinal < 0) || (ordinal >= MEMBER_LEN))
		return;
	*name = (const char *)MEMBER_LIST[ordinal];
	*length = (int)strlen(MEMBER_LIST[ordinal]);

}//end GetMemberName.

int H264v2Codec::SetMember(const char* type, void* pValue)
{
	int	 i;
	char *p = (char *)type;
	int  len = (int)strlen(p);

	if (strncmp(p, "autoiframedetectflag", len) == 0)
	{
    if (_autoIFrameIncluded == NULL) return(1);  ///< Do nothing.

    bool* pV = (bool *)pValue;
		for (i = 0; i < _mbLength; i++)
			_autoIFrameIncluded[i] = pV[i];
	}
  else if (strncmp(p, "roi multiplier", len) == 0)
  {
    if (_roiMultiplier == NULL)
    {
      _errorStr = "[H264v2Codec::SetMember] Region of interest array not active";
      return(0);
    }//end !_roiMultiplier...

    double* pV = (double *)pValue;
    for (i = 0; i < _mbLength; i++)
      _roiMultiplier[i] = pV[i];
  }
  else
	{
		_errorStr = "[H264v2Codec::SetMember] Write member not supported";
		return(0);
	}//end else...

	return(1);
}//end SetMember.

/**
-----------------------------------------------------------------------
  Public Implementation.
-----------------------------------------------------------------------
*/
/** Open the codec for encoding/decoding.
The state of the codec and memory allocations are made in this method and are
entirely dependent on the codec parameters. Using SetParameter() all parmeters
must be set prior to calling this methods.
@return : 1 = success, 0 = failure.
*/
int H264v2Codec::Open(void)
{
	int i;

	/// If already open then close first before continuing.
	if (_codecIsOpen)
		Close();

	/// --------------- Configure Sequence & Picture parameter sets -----------------
	/// The _genParamSetOnOpen parameter determines whether or not the seq/pic params 
	/// are generated and set in this call to Open(). If the param sets are to be generated 
	/// then the SetParameters() method calls must have set the appropriate codec params 
	/// prior to calling Open() as these settings are dependent on the codec parameters.
	/// If the param sets are not to be generated then the codec params must be extracted
	/// from the seq/pic param sets.
	if (_genParamSetOnOpen)
	{
		/// Sequence parameter set for Baseline profile.
		if (!SetSeqParamSet(_currSeqParam))
		{
			_errorStr = "[H264Codec::Open] Cannot set sequence parameter set";
			Close();
			return(0);
		}//end if !SetSeqParamSet...

		/// Picture parameter set.
		if (!SetPicParamSet(_currPicParam, _currSeqParam))
		{
			_errorStr = "[H264Codec::Open] Cannot set picture parameter set";
			Close();
			return(0);
		}//end if !SetPicParamSet...
	}//end if _genParamSetOnOpen...
	else
	{
		if (!GetCodecParams(_currPicParam))
		{
			/// Error string is set in the method.
			Close();
			return(0);
		}//end if !GetCodecParams...
	}//end else...

	/// --------------- Create encoded SPS and PPS streams ----------------------------------------
  /// Every IDR-Pic will have an SPS NAL, PPS NAL and an IDR NAL concatenated together in a single 
  /// stream. Create a cached param set for the current selection to use in the Code() method.
	if (_prependParamSetsToIPic)
	{
		int tempPicCodingType = _pictureCodingType;

		_pictureCodingType = H264V2_SEQ_PARAM;
		if (!CodeNonPicNALTypes((void *)_pEncSeqParam, H264V2_ENC_PARAM_LEN * 8))
		{
			/// Error string is set in the method.
			Close();
			return(0);
		}//end if !CodeNonPicNALTypes...
		_encSeqParamByteLen = GetCompressedByteLength();

		_pictureCodingType = H264V2_PIC_PARAM;
		if (!CodeNonPicNALTypes((void *)_pEncPicParam, H264V2_ENC_PARAM_LEN * 8))
		{
			/// Error string is set in the method.
			Close();
			return(0);
		}//end if !CodeNonPicNALTypes...
		_encPicParamByteLen = GetCompressedByteLength();

		/// Restore state.
		_pictureCodingType = tempPicCodingType;

	}//end if _prependParamSetsToIPic...

	  /// --------------- Alloc image memory ----------------------------------------------
	/// Create an image memory space.
	_lumWidth = _width;
	_lumHeight = _height;
	_chrWidth = _width / 2;
	_chrHeight = _height / 2;

	/// Alloc a large contiguous block with Lum first followed by ChrU then ChrV
	/// for both image blocks of input and reference.
	int lumSize = _lumWidth * _lumHeight;
	int chrSize = _chrWidth * _chrHeight;
	int imgSize = lumSize + 2 * chrSize;
	/// In/Out and ref images with primary lum at the head.
	_pLum = new short[2 * imgSize];
	if (!_pLum)
	{
		_errorStr = "[H264Codec::Open] Image memory unavailable";
		Close();
		return(0);
	}//end if !_pLum...
	  /// Place each image and colour component head pointer.
	_pChrU = &(_pLum[lumSize]);												///< End of _pLum.
	_pChrV = &(_pLum[lumSize + chrSize]);							///< End of _pChrU.
	_pRLum = &(_pLum[imgSize]);												///< End of input image.
	_pRChrU = &(_pLum[imgSize + lumSize]);						///< End of image and _pRLum.
	_pRChrV = &(_pLum[imgSize + lumSize + chrSize]);	///< End of _pRChrU.

  /// Zero the reference and the previous input image spaces. Note that the mem
	/// is contiguous.
	memset((void *)_pLum, 0, 2 * imgSize * sizeof(short));

	/// --------------- Configure the overlays to the img mem -------------------------
	/// The encoding/decoding of the residual image is performed on 4x4 blocks within
	/// each macroblock of the in/out and ref images.
	_Lum = new OverlayMem2Dv2(_pLum, _lumWidth, _lumHeight, 16, 16);
	_RefLum = new OverlayMem2Dv2(_pRLum, _lumWidth, _lumHeight, 16, 16);
	_Cb = new OverlayMem2Dv2(_pChrU, _chrWidth, _chrHeight, 8, 8);
	_RefCb = new OverlayMem2Dv2(_pRChrU, _chrWidth, _chrHeight, 8, 8);
	_Cr = new OverlayMem2Dv2(_pChrV, _chrWidth, _chrHeight, 8, 8);
	_RefCr = new OverlayMem2Dv2(_pRChrV, _chrWidth, _chrHeight, 8, 8);
	if ((_Lum == NULL) || (_RefLum == NULL) || (_Cb == NULL) ||
		(_RefCb == NULL) || (_Cr == NULL) || (_RefCr == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate image and reference overlay objects";
		Close();
		return(0);
	}//end if !_Lum...

	  /// --------------- Alloc and configure prediction mem data objects ---------------------
	_p16x16 = new short[256];	///< 16x16 block for predicition operations.
	_16x16 = new OverlayMem2Dv2(_p16x16, 16, 16, 16, 16);
	_p8x8_0 = new short[64];	///< 8x8 blocks for predicition operations.
	_8x8_0 = new OverlayMem2Dv2(_p8x8_0, 8, 8, 8, 8);
	_p8x8_1 = new short[64];
	_8x8_1 = new OverlayMem2Dv2(_p8x8_1, 8, 8, 8, 8);

	if ((_p16x16 == NULL) || (_16x16 == NULL) ||
		(_p8x8_0 == NULL) || (_8x8_0 == NULL) ||
		(_p8x8_1 == NULL) || (_8x8_1 == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot create prediction memory objects";
		Close();
		return(0);
	}//end if !_p16x16...

	/// ---------------- Configure Macroblock cache objects -----------------------------
	_mbImg = new H264MbImgCache();
	if (_mbImg == NULL)
	{
		_errorStr = "[H264Codec::Open] Cannot create macroblock image cache object";
		Close();
		return(0);
	}//end if !_mbImg...
	_mbImg->Create();

	/// --------------- Configure Macroblock data objects -----------------------------
	int mbWidth = _lumWidth / 16;		///< Note: Assuming mod 16 image dimensions (potential problem).
	int mbHeight = _lumHeight / 16;
	_mbLength = mbWidth * mbHeight;

	_pMb = new MacroBlockH264[_mbLength];
	_Mb = new MacroBlockH264*[mbHeight];	///< Address array.

	/// Only specified macroblocks are included in the detection of an I-frame. This
	/// flag list is used to indicate that inclusion.
	_autoIFrameIncluded = new bool[_mbLength];

	if ((_pMb == NULL) || (_Mb == NULL) || (_autoIFrameIncluded == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate macroblock data objects";
		Close();
		return(0);
	}//end if !_pMB...

	for (i = 0; i < mbHeight; i++)	///< Load the address array.
		_Mb[i] = &(_pMb[i * mbWidth]);

	/// Load the macroblock image mem 2-D offsets, indices and neighbourhood variables. There
	/// is only one slice (slice num = 0) and therefore extends from macroblock index 0..._mbLength-1.
	MacroBlockH264::Initialise(mbHeight, mbWidth, 0, _mbLength - 1, 0, _Mb);

	/// Load the flag for each macroblock that includes/excludes it from the 
	/// auto I-frame test during motion estimation. Default to include all.
	for (i = 0; i < _mbLength; i++)
		_autoIFrameIncluded[i] = 1;

	/// --------------- Configure colour converters ---------------------------------
	if (_inColour == H264V2_RGB24)
	{
		/// Encoder input.
#ifdef _CCIR601
		_pInColourConverter = new RealRGB24toYUV420CCIR601ConverterVer16(_width, _height, 128);
#else
		_pInColourConverter = new RealRGB24toYUV420ConverterImpl2Ver16(_width, _height, 128);
#endif

		if (_pInColourConverter == NULL)
		{
			_errorStr = "[H264Codec::Open] Cannot instantiate input colour converter";
			Close();
			return(0);
		}//end if !_pInColourConverter...

	  /// the calling code is responsible for the flipping of the image
		_pInColourConverter->SetFlip(_flip);
	}//end if _inColour...

	if (_outColour == H264V2_RGB24)
	{
		/// Decoder output.
#ifdef _CCIR601
		_pOutColourConverter = new RealYUV420toRGB24CCIR601ConverterVer16(_width, _height);
#else
		_pOutColourConverter = new RealYUV420toRGB24ConverterImpl2Ver16(_width, _height, 128);
#endif

		if (_pOutColourConverter == NULL)
		{
			_errorStr = "[H264Codec::Open] Cannot instantiate output colour converter";
			Close();
			return(0);
		}//end if !_pOutColourConverter...

	  /// the calling code is responsible for the flipping of the image
		_pOutColourConverter->SetFlip(_flip);

	}//end if _outColour...

	/// --------------- Instantiate IT filters ------------------------------------
	/// Create Integer Transformers (IT) and their inverses for AC and DC coeffs. The
	/// quantisers are included in the IT transform classes.
	_pF4x4TLum = new FastForward4x4ITImpl2();
	_pF4x4TChr = new FastForward4x4ITImpl2();
	_pFDC4x4T = new FastForwardDC4x4ITImpl1();
	_pFDC2x2T = new FastForwardDC2x2ITImpl1();
	_pI4x4TLum = new FastInverse4x4ITImpl1();
	_pI4x4TChr = new FastInverse4x4ITImpl1();
	_pIDC4x4T = new FastInverseDC4x4ITImpl1();
	_pIDC2x2T = new FastInverseDC2x2ITImpl1();

	if ((_pF4x4TLum == NULL) || (_pF4x4TChr == NULL) || (_pFDC4x4T == NULL) || (_pFDC2x2T == NULL) ||
		(_pI4x4TLum == NULL) || (_pI4x4TChr == NULL) || (_pIDC4x4T == NULL) || (_pIDC2x2T == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate Integer Transform filter objects";
		Close();
		return(0);
	}//end if !_pF4x4TLum...

	  /// Set default modes and added scaling for IT filters.
	_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_pFDC4x4T->SetMode(IForwardTransform::TransformAndQuant);
	_pFDC2x2T->SetMode(IForwardTransform::TransformAndQuant);
	_pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
	_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
	_pIDC4x4T->SetMode(IInverseTransform::TransformAndQuant);
	_pIDC2x2T->SetMode(IInverseTransform::TransformAndQuant);

	// --------------- Create the Vlc encoders and decoders --------------------------
	/// Create the vlc encoders and decoders for use with CAVLC.
	_pPrefixVlcEnc = new PrefixH264VlcEncoderImpl1();
	_pPrefixVlcDec = new PrefixH264VlcDecoderImpl1();
	_pCoeffTokenVlcEnc = new CoeffTokenH264VlcEncoder();
	_pCoeffTokenVlcDec = new CoeffTokenH264VlcDecoder();
	_pTotalZeros4x4VlcEnc = new TotalZeros4x4H264VlcEncoder();
	_pTotalZeros4x4VlcDec = new TotalZeros4x4H264VlcDecoder();
	_pTotalZeros2x2VlcEnc = new TotalZeros2x2H264VlcEncoder();
	_pTotalZeros2x2VlcDec = new TotalZeros2x2H264VlcDecoder();
	_pRunBeforeVlcEnc = new RunBeforeH264VlcEncoder();
	_pRunBeforeVlcDec = new RunBeforeH264VlcDecoder();

	/// Vlc encoder and decoder for the coded block pattern.
	_pBlkPattVlcEnc = new CodedBlkPatternH264VlcEncoder();
	_pBlkPattVlcDec = new CodedBlkPatternH264VlcDecoder();

	/// Vlc encoder and decoder for the delta QP.
	_pDeltaQPVlcEnc = new ExpGolombSignedVlcEncoder();
	_pDeltaQPVlcDec = new ExpGolombSignedVlcDecoder();

	/// Vlc encoder and decoder for the macroblock type.
	_pMbTypeVlcEnc = new ExpGolombUnsignedVlcEncoder();
	_pMbTypeVlcDec = new ExpGolombUnsignedVlcDecoder();

	/// Vlc encoder and decoder for intra chr pred mode. ExpGolomb codecs
	/// are stateless therefore they can be reused.
	_pMbIChrPredModeVlcEnc = _pMbTypeVlcEnc;
	_pMbIChrPredModeVlcDec = _pMbTypeVlcDec;

	/// Vlc encoder and decoder for motion vector differences. ExpGolomb codecs
	/// are stateless therefore they can be reused.
	_pMbMotionVecDiffVlcEnc = _pDeltaQPVlcEnc;
	_pMbMotionVecDiffVlcDec = _pDeltaQPVlcDec;

	/// Vlc encoder and decoder for general headers. ExpGolomb codecs
	/// are stateless therefore they can be reused.
	_pHeaderUnsignedVlcEnc = _pMbTypeVlcEnc;
	_pHeaderUnsignedVlcDec = _pMbTypeVlcDec;
	_pHeaderSignedVlcEnc = _pDeltaQPVlcEnc;
	_pHeaderSignedVlcDec = _pDeltaQPVlcDec;

	if ((_pPrefixVlcEnc == NULL) || (_pPrefixVlcDec == NULL) ||
		(_pCoeffTokenVlcEnc == NULL) || (_pCoeffTokenVlcDec == NULL) ||
		(_pTotalZeros4x4VlcEnc == NULL) || (_pTotalZeros4x4VlcDec == NULL) ||
		(_pTotalZeros2x2VlcEnc == NULL) || (_pTotalZeros2x2VlcDec == NULL) ||
		(_pRunBeforeVlcEnc == NULL) || (_pRunBeforeVlcDec == NULL) ||
		(_pBlkPattVlcEnc == NULL) || (_pBlkPattVlcDec == NULL) ||
		(_pDeltaQPVlcEnc == NULL) || (_pDeltaQPVlcDec == NULL) ||
		(_pMbTypeVlcEnc == NULL) || (_pMbTypeVlcDec == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate Vlc codec objects";
		Close();
		return(0);
	}//end if !_pPrefixVlcEnc...

	  /// Create a CAVLC encoder and decoder for each coeff block size.
	_pCAVLC4x4 = new CAVLCH264Impl();
	_pCAVLC2x2 = new CAVLCH264Impl();
	if ((_pCAVLC4x4 == NULL) || (_pCAVLC2x2 == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate CAVLC codec objects";
		Close();
		return(0);
	}//end if !_pCAVLC4x4...

	  /// Attach the vlc encoders and decoders to the associated CAVLC.
	_pCAVLC4x4->SetMode(CAVLCH264Impl::Mode4x4);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetTokenCoeffVlcEncoder(_pCoeffTokenVlcEnc);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetTokenCoeffVlcDecoder(_pCoeffTokenVlcDec);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetPrefixVlcEncoder(_pPrefixVlcEnc);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetPrefixVlcDecoder(_pPrefixVlcDec);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetRunBeforeVlcEncoder(_pRunBeforeVlcEnc);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetRunBeforeVlcDecoder(_pRunBeforeVlcDec);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetTotalZerosVlcEncoder(_pTotalZeros4x4VlcEnc);
	((CAVLCH264Impl *)_pCAVLC4x4)->SetTotalZerosVlcDecoder(_pTotalZeros4x4VlcDec);

	_pCAVLC2x2->SetMode(CAVLCH264Impl::Mode2x2);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetTokenCoeffVlcEncoder(_pCoeffTokenVlcEnc);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetTokenCoeffVlcDecoder(_pCoeffTokenVlcDec);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetPrefixVlcEncoder(_pPrefixVlcEnc);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetPrefixVlcDecoder(_pPrefixVlcDec);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetRunBeforeVlcEncoder(_pRunBeforeVlcEnc);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetRunBeforeVlcDecoder(_pRunBeforeVlcDec);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetTotalZerosVlcEncoder(_pTotalZeros2x2VlcEnc);
	((CAVLCH264Impl *)_pCAVLC2x2)->SetTotalZerosVlcDecoder(_pTotalZeros2x2VlcDec);

	// --------------- Configure bit stream access -----------------------------------
	_pBitStreamWriter = new BitStreamWriterMSB();
	_pBitStreamReader = new BitStreamReaderMSB();
	if ((_pBitStreamWriter == NULL) || (_pBitStreamReader == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate bit stream access objects";
		Close();
		return(0);
	}//end if !_pBitStreamWriter...

	  /// --------------- Configure motion estimators -----------------------------------

	/// Create a motion vector predictor for the motion estimator to use in biasing towards
	/// the predicted vector when distortion choise is ambiguous.
	_pMotionPredictor = new H264MotionVectorPredictorImpl1(_pMb);
	if (!_pMotionPredictor)
	{
		_errorStr = "[H264Codec::Open] Cannot create motion vector predictor object";
		Close();
		return(0);
	}//end if !_pMotionPredictor...

	  /// Select an appropriate motion estimator.
	int motionVectorRange;	///< In 1/4 pel units but also required for motion compensator in full pel units.
	if ((_width <= 1408) && (_height <= 1152))
		motionVectorRange = 512;	///< 512/4 = [-128.00 ... 127.75], 192/4 = [-48.00 ... 47.75]
	else
		motionVectorRange = 1024;	///< 1024/4 =[-256.00 ... 255.75], 256/4 =[-64.00 ... 63.75]

  switch (_motionEstimationType)
  {
    case H264V2_MOTION_FULL:
      {
        /// Slowest full accurate estimator.
        _pMotionEstimator = new MotionEstimatorH264ImplFull( (const void *)_pLum,
        																										 (const void *)_pRLum,
        																										 _lumWidth,
        																										 _lumHeight,
        																										 motionVectorRange, ///< In 1/4 pel units.
                                                             _pMotionPredictor,
        																										 _autoIFrameIncluded);
        /// Implementation specific modes.
        if(_pMotionEstimator != NULL)
          _pMotionEstimator->SetMode(0);	///< Auto mode.
      }//end block...
      break;
    case H264V2_MOTION_FULL_MULTIRES:
      {
        /// Slow more accurate multiresolution estimator.
        _pMotionEstimator = new MotionEstimatorH264ImplMultires((const void *)_pLum,	///< Multi res estimation.
        																												(const void *)_pRLum,
        																												_lumWidth,
        																												_lumHeight,
        																												motionVectorRange, ///< In 1/4 pel units
                                                                _pMotionPredictor,
                                                                _autoIFrameIncluded);
        /// Implementation specific modes.
        if (_pMotionEstimator != NULL)
          _pMotionEstimator->SetMode(0);	///< mode 0 = 1/4 pel, mode 1 = 1/2 pel, mode 2 = full pel.
      }//end block...
      break;
    case H264V2_MOTION_UMHS_PARTIAL:
      {
        /// Cross search algorithm with partial sums as defined in the std reference implementations of H264
        _pMotionEstimator = new MotionEstimatorH264ImplUMHS((const void *)_pLum,
                                                            (const void *)_pRLum,
                                                            _lumWidth,
                                                            _lumHeight,
                                                            motionVectorRange, ///< In 1/4 pel units.
                                                            _pMotionPredictor,
                                                            _autoIFrameIncluded,
                                                            _pMb);
        /// Implementation specific modes.
        if (_pMotionEstimator != NULL)
          _pMotionEstimator->SetMode(_motionResolution);	///< Estimation pel resolution: 0=1/4 pel, 1=1/2 pel, 2=full pel.
      }///end block...
      break;
    case H264V2_MOTION_FHS_PARTIAL:
    {
      /// Fasthegagon sequencing search algorithm with partial sums
      _pMotionEstimator = new MotionEstimatorH264ImplFHS((const void *)_pLum,
                                                         (const void *)_pRLum,
                                                          _lumWidth,
                                                          _lumHeight,
                                                          motionVectorRange, ///< In 1/4 pel units.
                                                          _pMotionPredictor,
                                                          _autoIFrameIncluded,
                                                          _pMb);
      /// Implementation specific modes.
      if (_pMotionEstimator != NULL)
        _pMotionEstimator->SetMode(_motionResolution);	///< Estimation pel resolution: 0=1/4 pel, 1=1/2 pel, 2=full pel.
    }///end block...
    break;
    case H264V2_MOTION_CROSS_PARTIAL:
    default:  /// H264V2_MOTION_CROSS_PARTIAL
      {
        /// Cross search algorithm with partial sums as defined in the std reference implementations of H264
        _pMotionEstimator = new MotionEstimatorH264ImplCross( (const void *)_pLum,
                                                              (const void *)_pRLum,
                                                              _lumWidth,
                                                              _lumHeight,
                                                              motionVectorRange, ///< In 1/4 pel units.
                                                              _pMotionPredictor,
                                                              _autoIFrameIncluded);
        /// Implementation specific modes.
        if (_pMotionEstimator != NULL)
          _pMotionEstimator->SetMode(_motionResolution);	///< Estimation pel resolution: 0=1/4 pel, 1=1/2 pel, 2=full pel.
      }///end block...
      break;
  }///end switch _motionEstimationType...

  /// Fast less accurate estimator.
	//_pMotionEstimator = new MotionEstimatorH264ImplMultiresCrossVer2( (const void *)_pLum,	///< Multi res estimation.
	//	                                                                (const void *)_pRLum,
	//	                                                                _lumWidth,
	//	                                                                _lumHeight,
	//	                                                                motionVectorRange, ///< In 1/4 pel units.
	//	                                                                _pMotionPredictor,
	//	                                                                _autoIFrameIncluded);

	//	_pMotionEstimator = new MotionEstimatorH264ImplMultiresCross(	(const void *)_pLum,	///< Multi res estimation.
	//																																(const void *)_pRLum,
	//																																_lumWidth,
	//																																_lumHeight,
	//																																motionVectorRange, ///< In 1/4 pel units.
	//																																_autoIFrameIncluded);



		/// Test motion estimator for collecting data. Full pel only full search based estimation.
	//	_pMotionEstimator = new MotionEstimatorH264ImplTest(	(const void *)_pLum,	///< Multi res estimation.
	//																												(const void *)_pRLum,
	//																												_lumWidth,
	//																												_lumHeight,
	//																												motionVectorRange, ///< In 1/4 pel units.
	//                                                        _pMotionPredictor,
	//																												_autoIFrameIncluded);

	if (_pMotionEstimator != NULL)
	{
		//_motionFactor = 2;	///< Abs diff algorithm.
		_motionFactor = 4;	///< Sqr err algorithm.

		if (!_pMotionEstimator->Create())
		{
			_errorStr = "[H264Codec::Open] Cannot create motion estimator";
			Close();
			return(0);
		}//end if !Create...
	}//end if _pMotionEstimator...
	else
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate motion estimator object";
		Close();
		return(0);
	}//end if else...

	  /// --------------- Configure motion compensator -----------------------------------
	  /// The motion compensator requires the range param to be in full pel units.	
	_pMotionCompensator = new MotionCompensatorH264ImplStd(motionVectorRange / 4);

	if (_pMotionCompensator != NULL)
	{
		if (!_pMotionCompensator->Create((void *)_pRLum, _lumWidth, _lumHeight, 16, 16))
		{
			_errorStr = "[H264Codec::Open] Cannot create motion compensator";
			Close();
			return(0);
		}//end if !Create...
	}//end if _pMotionCompensator...
	else
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate motion compensator object";
		Close();
		return(0);
	}//end if else...

	  /// Create a motion vector list to hold the decoded vectors for the compensation process.
	_pMotionVectors = new VectorStructList(VectorStructList::SIMPLE2D);
	if (!_pMotionVectors)
	{
		_errorStr = "[H264Codec::Open] Cannot create motion vector list object";
		Close();
		return(0);
	}//end if !_pMotionVectors...
	if (!_pMotionVectors->SetLength(mbWidth * mbHeight)) ///< One per Mb for 16x16 vectors only.
	{
		_errorStr = "[H264Codec::Open] Insufficient mem for motion vector list";
		Close();
		return(0);
	}//end if !SetLength...

	/// --------------- Create image plane encoders and decoders ----------------------
	/// DECODERS: Select an Intra Decoder.
	_pIntraImgPlaneDecoder = new IntraImgPlaneDecoderImplStdVer1(this);
	/// Set the Inter decoder.
	_pInterImgPlaneDecoder = new InterImgPlaneDecoderImplStdVer1(this);

	/// ENCODERS: Select an Intra and Inter Encoder depending on the mode.
	if (_modeOfOperation == H264V2_FIXED_QP)
	{
		_pIntraImgPlaneEncoder = new IntraImgPlaneEncoderImplStdVer1(this);
		_pInterImgPlaneEncoder = new InterImgPlaneEncoderImplStdVer1(this);
	}//end if _modeOfOperation...
	else if (_modeOfOperation == H264V2_MINMAX_EXACT)
	{
		_pQuant = 26; ///< Start in the middle before adaptation of each mb QP value.
		_pIntraImgPlaneEncoder = new IntraImgPlaneEncoderImplMinMax(this);
		_pInterImgPlaneEncoder = new InterImgPlaneEncoderImplMinMax(this);
	}//end else...
	else	///< if((_modeOfOperation == H264V2_DMAX)||(_modeOfOperation == H264V2_MINMAX_RATECNT)||(_modeOfOperation == H264V2_MINAVG_RATECNT))
	{
		_pQuant = 26; ///< Start in the middle before adaptation of each mb QP value.
		_pIntraImgPlaneEncoder = new IntraImgPlaneEncoderImplDMax(this);
		_pInterImgPlaneEncoder = new InterImgPlaneEncoderImplDMax(this);

		if ((_modeOfOperation == H264V2_MINMAX_RATECNT)||(_modeOfOperation == H264V2_MINAVG_RATECNT))
		{
			switch (_rateControlModelType)
			{
			case H264V2_RATE_CONTROL_MODEL_QUAD:
        _pRateCntlIFrames = new RateControlImplQuad(64.0, 85000.0);
        _pRateCntlPFrames = new RateControlImplQuad(-48.0, 145000.0);
				break;
			case H264V2_RATE_CONTROL_MODEL_POW:
				_pRateCntlIFrames = new RateControlImplPow(-0.64, 15.0);
				_pRateCntlPFrames = new RateControlImplPow(-2.4, 176000.0);
				break;
			case H264V2_RATE_CONTROL_MODEL_LOG:
				_pRateCntlIFrames = new RateControlImplLog(0.2, 1.4);
				_pRateCntlPFrames = new RateControlImplLog(0.3, 1.8);
				break;
      /*
			case H264V2_RATE_CONTROL_MODEL_MULTI:
			  {
				  double modelParam[RateControlImplMultiModel::MODELS][2];  ///< Initial a and b model parameter per model.
				  modelParam[RateControlImplMultiModel::QUAD][0] = 40.0;
				  modelParam[RateControlImplMultiModel::QUAD][1] = 20000.0;
				  modelParam[RateControlImplMultiModel::POW][0] = 65.0;
				  modelParam[RateControlImplMultiModel::POW][1] = -0.9;
				  modelParam[RateControlImplMultiModel::LOG][0] = 0.1;
				  modelParam[RateControlImplMultiModel::LOG][1] = 1.2;
				  _pRateCntlIFrames = new RateControlImplMultiModel(modelParam, RateControlImplMultiModel::MODELS);
				  modelParam[RateControlImplMultiModel::QUAD][0] = 40.0;
				  modelParam[RateControlImplMultiModel::QUAD][1] = 20000.0;
				  modelParam[RateControlImplMultiModel::POW][0] = 15000.0;
				  modelParam[RateControlImplMultiModel::POW][1] = -1.3;
				  modelParam[RateControlImplMultiModel::LOG][0] = 0.2;
				  modelParam[RateControlImplMultiModel::LOG][1] = 1.4;
				  _pRateCntlPFrames = new RateControlImplMultiModel(modelParam, RateControlImplMultiModel::MODELS);
			  }
			break;
      */
			}//end switch _rateControlModelType...

			if ((_pRateCntlPFrames != NULL) && (_pRateCntlIFrames != NULL))
			{
				if ((!_pRateCntlPFrames->Create(_numRateCntlFrames)) || (!_pRateCntlIFrames->Create(_numRateCntlFrames)))
				{
					_errorStr = "[H264Codec::Open] Cannot create rate controllers";
					Close();
					return(0);
				}//end if !Create...
				switch (_rateControlModelType)
				{
				case H264V2_RATE_CONTROL_MODEL_QUAD:
					_pRateCntlIFrames->SetRDLimits(24.0, 0.0001, 4000000.0, 32.0);  ///< Quad Abs Diff limits.
					_pRateCntlPFrames->SetRDLimits(24.0, 0.0001, 4000000.0, 32.0);
					break;
				case H264V2_RATE_CONTROL_MODEL_POW:
				case H264V2_RATE_CONTROL_MODEL_LOG:
				/*case H264V2_RATE_CONTROL_MODEL_MULTI:*/
					_pRateCntlIFrames->SetRDLimits(24.0, 0.0001, 67108864.0, 256.0);  ///< Multi-model Sqr Diff limits
					_pRateCntlPFrames->SetRDLimits(24.0, 0.0001, 67108864.0, 256.0);
					break;
				}//end switch _rateControlModelType...
			}//end if _pRateCntlPFrames...
			else
			{
				_errorStr = "[H264Codec::Open] Cannot instantiate rate controllers";
				Close();
				return(0);
			}//end else...
		}//end if _modeOfOperation...
	}//end else...

	if ((_pIntraImgPlaneEncoder == NULL) || (_pIntraImgPlaneDecoder == NULL) ||
		(_pInterImgPlaneEncoder == NULL) || (_pInterImgPlaneDecoder == NULL))
	{
		_errorStr = "[H264Codec::Open] Cannot instantiate image plane encoder and decoder objects";
		Close();
		return(0);
	}//end if !_pIntraImgPlaneEncoder...
	if ((!_pIntraImgPlaneEncoder->Create(_mbLength)) ||
		(!_pInterImgPlaneEncoder->Create(_mbLength)))
	{
		_errorStr = "[H264Codec::Open] Cannot create image plane encoders";
		Close();
		return(0);
	}//end if !_pIntraImgPlaneEncoder...

   /// --------------- Create Region of Interest members ----------------------
  if (_enableROIEncoding)
  {
    _roiMultiplier = new double[_mbLength];
    if (_roiMultiplier == NULL)
    {
      _errorStr = "[H264Codec::Open] Cannot create region of interest members";
      Close();
      return(0);
    }//end if !_roiMultiplier...

     /// Default implementation is defining a focal point mb with a decay function from that point.

     /// Select the focal point.
    H264V2_COORD focalPt = FocalPoint();
    /// Find a normalisation constant to restrict the ROI map values to [0.0...1.0]. The default 
    /// implementation is the euclidian distance from the focal point to the furtherest boundary.
    /// The multiplier is 1 + DecayFunction().
    double norm = NormalisationConstant(focalPt);
    for (int i = 0; i < mbHeight; i++)
      for (int j = 0; j < mbWidth; j++)
        _roiMultiplier[i*mbWidth + j] = 1.0 + (2.0 * DecayFunction(H264V2_COORD{ j,i }, focalPt, norm));

  }//end if _enableROIEncoding...

  /// ---------------- Start at the beginning ----------------------------------
	_lastPicCodingType = H264V2_INTRA;
	_prevMotionDistortion = -1;
	_maxFrameNum = 1 << (_seqParam[_currSeqParam]._log2_max_frame_num_minus4 + 4);	///< Max limit (modulus) for _frameNum.
	_idrFrameNum = 0;	///< The starting I-frame counter that must then be different for every contiguous I-frame.
	Restart();

	/// Set up the optimisation time limit timer. If it does not exist then force the parameter to zero.
	if (!SetCounter())
		_timeLimitMs = 0;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	/*
	  BlockH264* pB1 = new BlockH264(4, 4);
	  BlockH264* pB2 = new BlockH264(4, 4);
	  short* pB1Mem = pB1->GetBlk();

	  /// Fill with random numbers between -250..250.
	//  srand( 34594938 );
	  srand( 2439967 );
	  for (i = 0; i < 16; i++ )
	  {
		int u = (double)rand() / (RAND_MAX + 1) * (250 - (-250))
			  + (-250);
		pB1Mem[i] = short(u);
	  }//end for i...
	  pB2->CopyBlock(pB1);  ///< Hold a copy to compare with.
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/InputBlk.csv", "Input Blk");

	  /// Forward DCT QP=1
		_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	  _pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	  _pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, 1);
	  pB1->ForwardTransform(_pF4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/DCTBlk.csv", "DCT Blk");
		_pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
	  pB1->ForwardTransform(_pF4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/DCTQP1Blk.csv", "DCT QP1 Blk");

	  /// Inverse DCT QP=1
	  _pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, 1);
		_pI4x4TLum->SetMode(IInverseTransform::QuantOnly);
	  pB1->InverseTransform(_pI4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/IQP1Blk.csv", "IQP1 Blk");
		_pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
	  pB1->InverseTransform(_pI4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/OutputQP1Blk.csv", "Output QP1 Blk");

	  /// Reload input.
	  pB1->CopyBlock(pB2);
	  /// Forward DCT QP=40
	  _pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, 40);
	  pB1->ForwardTransform(_pF4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/DCTQP40Blk.csv", "DCT QP40 Blk");

	  /// Inverse DCT QP=40
	  _pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, 40);
		_pI4x4TLum->SetMode(IInverseTransform::QuantOnly);
	  pB1->InverseTransform(_pI4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/IQP40Blk.csv", "IQP40 Blk");
		_pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
	  pB1->InverseTransform(_pI4x4TLum);
	  DumpBlock(pB1->GetBlkOverlay(), "c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/OutputQP40Blk.csv", "Output QP40 Blk");

	  delete pB2;
	  delete pB1;
	*/
	////////////////////////////////////////////////////////////////////////////////////////////////////

	_errorStr = "[H264Codec::Open] No Erorr";
	_codecIsOpen = 1;
	return(1);
}//end Open.

/** Encode one frame of pels to a compressed stream.
The input source pels in the format specified by "incolour" parameter are compressed
to the output stream. The codeParameter specifies either the max size in bits of the
pCmp buffer or the number of bits to target during the compression process. The interpretation
is determined by the "mode of operation" parameter. For SPS and PPS encoding the input
pSrc is ignored.
@param  pSrc          : Input raw pels of one complete frame.
@param  pCmp          : Output compressed stream buffer.
@param  codeParameter : Mode of operation based bit size limits.
@return               : 1 = success, 0 = failure.
*/
int	H264v2Codec::Code(void* pSrc, void* pCmp, int codeParameter)
{
	int allowedBits, bitsUsed;

	if ((_pictureCodingType != H264V2_INTRA) && (_pictureCodingType != H264V2_INTER))
		return(CodeNonPicNALTypes(pCmp, codeParameter));

	if (!_codecIsOpen)
	{
		_errorStr = "[H264V2Codec::Code] Codec is not open";
		return(0);
	}//end if !_codecIsOpen...

	/// Only baseline profile is supported: _profile_idc = 66.
	if (_seqParam[_currSeqParam]._profile_idc != 66)
	{
		_errorStr = "[H264Codec::Code] This implementation only supports the baseline profile";
		return(0);
	}//end if _profile_idc not baseline...

  /// Mark the start time of the encoding process.
	if (_timeLimitMs)
		_startTime = (int)GetCounter();
#ifdef H264V2_DUMP_TIMING
	_startTime = (int)GetCounter();
#endif

	/// Interpret the code parameter as a frame bit limit or avg bit rate.
	int frameBitLimit = codeParameter;
	_avgBitsPerFrame = codeParameter;
	double avgBppRate = (double)(_avgBitsPerFrame) / (double)(_lumWidth*_lumHeight);  ///< Bit rate in bits per pel.
	if ((_modeOfOperation == H264V2_MINMAX_RATECNT)||(_modeOfOperation == H264V2_MINAVG_RATECNT))
		frameBitLimit = _maxBitsPerFrame; ///< Bit limit must be made sufficiently large for variations in the buffered rate per frame.

	int runOutOfBits = 0;
	int bitLimit = frameBitLimit - 0; ///< Allow some slack for the expected trailing picture bits.
	_bitStreamSize = 0;

	/// Reset the stream writer with the frame bit limit. 
	_pBitStreamWriter->SetStream(pCmp, frameBitLimit);

	/// Only IDR and P pictures are supported.
	if ((_pictureCodingType != H264V2_INTRA) && (_pictureCodingType != H264V2_INTER))
	{
		_errorStr = "[H264v2Codec::Code] Picture coding type not supported";
		return(0);
	}//end if !H264V2_INTRA...

   ///-------------- Colour Space Conversion -----------------------------------------------
  if (_inColour == H264V2_YUV420P16)	      ///< The natural colour space of the encoder with type = short.
		memcpy((void *)_pLum, (const void *)pSrc, ((_lumWidth * _lumHeight) + 2 * (_chrWidth * _chrHeight)) * sizeof(short));
	else if (_inColour == H264V2_YUV420P8)  ///< ...type = byte.
	{
    if (_flip)
    {
      unsigned char *pl = (unsigned char *)pSrc;
      unsigned char *pu = &(pl[_lumWidth * _lumHeight]);
      unsigned char *pv = &(pu[_chrWidth * _chrHeight]);

      int row, rrow, col;

      for (row = 0, rrow = (_lumHeight - 1); row < _lumHeight; row++, rrow--)
        for (col = 0; col < _lumWidth; col++)
          _pLum[row*_lumWidth + col] = (short)(pl[rrow*_lumWidth + col]);

      for (row = 0, rrow = (_chrHeight - 1); row < _chrHeight; row++, rrow--)
        for (col = 0; col < _chrWidth; col++)
        {
          _pChrU[row*_chrWidth + col] = (short)(pu[rrow*_chrWidth + col]);
          _pChrV[row*_chrWidth + col] = (short)(pv[rrow*_chrWidth + col]);
        }//end for col...
    }//end if flip...
    else
    {
      int colLen = (_lumWidth * _lumHeight) + 2 * (_chrWidth * _chrHeight);
      for (int i = 0; i < colLen; i++)
        _pLum[i] = (short)((unsigned char *)pSrc)[i];
    }//end else...
	}//end if H264V2_YUV420P8...
	else
		_pInColourConverter->Convert((void *)pSrc, (void *)_pLum, (void *)_pChrU, (void *)_pChrV);

  ///-------------- Motion Estimation -----------------------------------------------
  /// Motion estimation is used to determine if an IDR frame should be inserted.
	if (_pictureCodingType == H264V2_INTER)
	{
		/// Motion estimation.
		long motionDistortion = 0;

		/// The estimator implementation was chosen in Open() depending on the mode selected.
		if ((_modeOfOperation == H264V2_MINMAX_RATECNT)||(_modeOfOperation == H264V2_MINAVG_RATECNT))
		{
			/// Motion vector lambda adaptation in this mode uses a TCP-like algorithm. Note that it uses the most recent past
      /// P-frame rate control results applied to this P-frame.
			double deltaLambda = 0.0;
			if (_pRateCntlPFrames->OutOfBounds() && _pRateCntlPFrames->UpperDistortionOverflow())
				deltaLambda = MVLAMBDA_MULT*(MVLAMBDA_MAX - _mvLambda);
			else if ((_pRateCntlPFrames->OutOfBounds() && _pRateCntlPFrames->LowerDistortionOverflow()) || (_mvLambda > MVLAMBDA_STEADYSTATE))
				deltaLambda = MVLAMBDA_STEP;
			_mvLambda += deltaLambda;
			if (_mvLambda < 0.0) _mvLambda = 0.0;

			_pMotionEstimationResult = (VectorStructList *)(_pMotionEstimator->Estimate(&motionDistortion, (void *)(&_mvLambda)));
		}//end if H264V2_MINMAX_RATECNT...
		else
			_pMotionEstimationResult = (VectorStructList *)(_pMotionEstimator->Estimate(&motionDistortion));

		/// The estimation results are processed into an encoded structure list. A 
		/// decision is made on the type of encoding as predictive or basic and 
		/// returns the selection. The _Motion member reflects the choice.

		///----------------- Auto I-picture detection ------------------------------------
		if (_autoIPicture && (_prevMotionDistortion != -1)) ///< Previous frame was not an IDR-Picture.
		{
			/// Test for an IDR-picture.
			if (motionDistortion > (_motionFactor * _prevMotionDistortion))
        if( motionDistortion > (long)(0.5 * sqrt((double)(_width*_height))) )
				  _pictureCodingType = H264V2_INTRA;
		}///end if _prevMotionDistortion...

		_prevMotionDistortion = motionDistortion;

		/// Force an I-picture if frame counter has wraped around in the modulus calculation.
		if (_frameNum == 0)
			_pictureCodingType = H264V2_INTRA;

	}//end if H264V2_INTER...

	/// The motion estimation process is used to determine if this picture
	/// should rather be coded as an IDR picture. Therefore the INTRA 
	/// picture type coding is done afterwards. 

  ///-------------- Pre-encoding rate control ---------------------------------
  if ((_modeOfOperation == H264V2_MINMAX_RATECNT)||(_modeOfOperation == H264V2_MINAVG_RATECNT))
  {
    /// Reset global frame accumulator.
    _frameMSD = 0;
    _frameMAD = 0;
    _frameMAD_N = 0;
    _predFrmMAD = 0.0;

    if (_pictureCodingType == H264V2_INTER)
    {
      /// Distortion is modified only when there is valid data in the rate control model. 
      /// Otherwise it remains at the previous frame's settings.
      if (_pRateCntlPFrames->ValidData())
      {
        double upperLimit = avgBppRate*(1.0 + ((double)_rateOvershootPercent / 100.0));
        _dMax = _pRateCntlPFrames->PredictDistortion(avgBppRate, upperLimit);
      }//end if ValidData...
    }//end if H264V2_INTER...
    else if (_pictureCodingType == H264V2_INTRA)
    {
      if (_pRateCntlIFrames->ValidData())
      {
        /// Readjust average frame bits with multiplier.fraction factor.
        avgBppRate = (avgBppRate * (double)_iPictureMultiplier) + ((avgBppRate * (double)_iPictureFraction) / 10.0);
        double upperLimit = avgBppRate*(1.0 + ((double)_rateOvershootPercent / 100.0));
        _dMax = _pRateCntlIFrames->PredictDistortion(avgBppRate, upperLimit);
      }//end if ValidData...
    }//end else if H264V2_INTRA...
  }//end if H264V2_MINMAX_RATECNT...

  ///-------------- Headers and Preparation  ---------------------------------
  /// For INTRA pictures and before any encoding begins readjust the 
	/// allowable bits based on the I-picture multiplier parameters.
	if (_pictureCodingType == H264V2_INTRA)
	{
		/// Continuously divide by 10 until in the range 0..9.
		while (_iPictureFraction > 10)
			_iPictureFraction = (_iPictureFraction / 10);
		/// Readjust frame bits with multiplier.fraction factor. Protect against excessively large bit limits that result in integer wrapping.
		int prior = frameBitLimit;
		frameBitLimit = (frameBitLimit * _iPictureMultiplier) + ((frameBitLimit * _iPictureFraction) / 10);
		if (frameBitLimit < prior) frameBitLimit = prior;
		_pBitStreamWriter->SetStreamBitSize(frameBitLimit);
		/// New bit limit setting.
		bitLimit = frameBitLimit - 0; ///< Allow some slack for the expected trailing picture bits.
	  /// Reset the frame number for I-pics.
		_frameNum = 0;

		/// Prepend SPS and PPS to the I-picture.
		if (_prependParamSetsToIPic)
		{
			allowedBits = bitLimit - _bitStreamSize;
			int paramTotBitLen = 8 * (_encSeqParamByteLen + _encPicParamByteLen);
			if (allowedBits < paramTotBitLen)
			{
				_errorStr = "[H264V2Codec::Code] Cannot prepend SPS and PPS to I-Picture stream";
				return(0);
			}//end if allowedBits...

			/// Write the pre-encoded SPS to the stream.
			int i;
			for (i = 0; i < _encSeqParamByteLen; i++)  ///< One byte at a time.
				_pBitStreamWriter->Write(8, _pEncSeqParam[i]);

			/// Write the pre-encoded PPS to the stream.
			for (i = 0; i < _encPicParamByteLen; i++)  ///< One byte at a time.
				_pBitStreamWriter->Write(8, _pEncPicParam[i]);

			_bitStreamSize += paramTotBitLen;

		}//end if _prependParamSetsToIPic...
	}//end if H264V2_INTRA...

  /// Write the 32-bit start code 0x00000001 to the stream.
	allowedBits = bitLimit - _bitStreamSize;
	if (allowedBits < 32)
	{
		_errorStr = "[H264V2Codec::Code] Cannot write start code to stream";
		return(0);
	}//end if allowedBits...
	_pBitStreamWriter->Write(32, 1);
	_bitStreamSize += 32;

	/// Define the NAL unit header based on the seleceted picture coding type. The final NAL type
	/// is only known at this point in the process.
	if (_pictureCodingType == H264V2_INTER)
	{
		_nal._ref_idc = 2;  ///< Non-zero for all referenced frames P and I.
		_nal._unit_type = NalHeaderH264::NonIDR_NoPartition_Slice;
	}//end if H264V2_INTER...
	else	///< if(_pictureCodingType == H264V2_INTRA)
	{
		_nal._ref_idc = 3;  ///< Non-zero for all referenced frames P and I.
		_nal._unit_type = NalHeaderH264::IDR_Slice;
	}//end else...

	/// Write the NAL header to the stream. 
	allowedBits = bitLimit - _bitStreamSize;
	runOutOfBits = WriteNALHeader(_pBitStreamWriter, allowedBits, &bitsUsed);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
		return(0);

	/// Write (concatinate) the slice header layer with its header flags to
	/// the stream. There is only one slice so the header, macroblocks (slice
	/// data) and tail may be coded in a linear order.
	_slice._frame_num = _frameNum;
	_slice._idr_pic_id = _idrFrameNum;
	/// Force the image plane encoders to use _pQuant as the slice qp and therefore the
	/// slice header is determined before encoding and may be added to the bit stream here.
	_slice._qp = _pQuant;
	_slice._qp_delta = _slice._qp - (_picParam[_currPicParam]._pic_init_qp_minus26 + 26);
	_slice._pic_parameter_set_id = _currPicParam;
	/// Baseline profile only has two slice types and in this implementation they are
	/// used for the entire frame/picture.
	if (_pictureCodingType == H264V2_INTER)
		_slice._type = SliceHeaderH264::P_Slice_All;
	else
		_slice._type = SliceHeaderH264::I_Slice_All;

	allowedBits = bitLimit - _bitStreamSize;
	runOutOfBits = WriteSliceLayerHeader(_pBitStreamWriter, allowedBits, &bitsUsed);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
		return(0);

	/// Encode the entire picture. The plane encoders do not write to the stream
	/// but do require to know the available bits. Allowance is made for the single
	/// trailing bit.
	allowedBits = bitLimit - _bitStreamSize - 1;

	///-------------- Encoding process ---------------------------------
	if (_pictureCodingType == H264V2_INTRA)
	{
		_prevMotionDistortion = -1;
		Restart(); ///< Reset the loop for I-picture.

		// The encoder was chosen in Open() depending on the mode selected. It
		// operates on the full list of slices and their macroblocks.
		if (!_pIntraImgPlaneEncoder->Encode(allowedBits, &bitsUsed, 1))
			return(0);	///< An error has occured.
	}//end if H264V2_INTRA...
	else if (_pictureCodingType == H264V2_INTER)
	{
    /// The encoder was chosen in Open() depending on the mode selected. It
		/// operates on the list of macroblocks. Motion compensation is included.
		if (!_pInterImgPlaneEncoder->Encode(allowedBits, &bitsUsed, 3))
			return(0);	///< An error has occured.
	}//end else H264V2_INTER...

  ///-------------- Write to stream ---------------------------------
	/// Write (concatinate) the macroblock layer (slice data) with its header 
	/// flags to the stream.
	runOutOfBits = WriteSliceDataLayer(_pBitStreamWriter, allowedBits, &bitsUsed);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
		return(0);

	/// Write (concatinate) the slice trailing bits to the stream. This is a min
  /// of 1 bit + zero bits to the end of the byte boundary.
	allowedBits = bitLimit - _bitStreamSize;
	runOutOfBits = WriteTrailingBits(_pBitStreamWriter, allowedBits, &bitsUsed);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
		return(0);

	/// Prevent start code emulation within the coded bit stream. The extra byte added
	/// to prevent the emulation is not counted as part of the bit written.
	if (_startCodeEmulationPrevention)
	{
		int offset = 0;
		if ((_pictureCodingType == H264V2_INTRA) && (_prependParamSetsToIPic))
			offset = _encSeqParamByteLen + _encPicParamByteLen;

		_bitStreamSize += InsertEmulationPrevention(_pBitStreamWriter, offset);
	}//end if _startCodeEmulationPrevention...

	/// In-loop filter for 4x4 block boundaries to remove blocking artefacts.
	if (_slice._disable_deblocking_filter_idc != 1)
		ApplyLoopFilter();

	///-------------- Post-encoding rate control ---------------------------------
	if ((_modeOfOperation == H264V2_MINMAX_RATECNT)|| (_modeOfOperation == H264V2_MINAVG_RATECNT))
	{
		/// Frame energy measured as mean abs and squared difference. Members were set per mb in the ProcessInterMbImplStd() method.
		double frmMAD = (double)(_frameMAD);
		double frmMSD = (double)(_frameMSD);
		if (_frameMAD_N)
		{
			frmMAD = frmMAD / (double)(_frameMAD_N);
			frmMSD = frmMSD / (double)(_frameMAD_N);
		}//end if _frameMAD_N...

		IRateControl* lclRateCntrl = _pRateCntlPFrames; ///< Default to P-Frames.
		if (_pictureCodingType == H264V2_INTER)
		{
			/// Reset the P-Frame RDO model after I-Frames.
	    //if(_lastPicCodingType == H264V2_INTRA)
	    //  _pRateCntlPFrames->Reset();
		}//end if H264V2_INTER...
    else if (_pictureCodingType == H264V2_INTRA)
    {
      /// Inform the P-frame rate controller that things will be different now that there is probably a new scene sequence.
      _pRateCntlPFrames->SignalSceneChange(frmMSD, frmMAD);  
      lclRateCntrl = _pRateCntlIFrames;
    }//end else if H264V2_INTRA...

		/// Store total rate, coeff rate, distortion, mean difference (and header rate).
		double rateBpp = (double)_bitStreamSize / (double)(_lumWidth*_lumHeight);
    /// For MINAVG the distortion measure is the avg squared/abs error per mb. For all other MINMAX
    /// it is worst case squared/abs error per mb.
    if (_modeOfOperation != H264V2_MINAVG_RATECNT)
      _frameDistortion = _dMax;

    if(_rateControlModelType == H264V2_RATE_CONTROL_MODEL_QUAD) ///< Uses MAD distortion measures.
		  lclRateCntrl->StoreMeasurements(rateBpp, _coeffBitsPerPel, (double)_frameDistortion, frmMSD, frmMAD);
    else
      lclRateCntrl->StoreMeasurements(rateBpp, rateBpp, (double)_frameDistortion, frmMSD, frmMAD);

	}//end if H264V2_MINMAX_RATECNT...

  ///-------------- Prepare for next frame ---------------------------------

	/// Any param changes required for the next picture encoding are done here.
	/// INTER pictures by default follow INTRA pictures.
	int tmpLastPicCodingType = _lastPicCodingType;
	_lastPicCodingType = _pictureCodingType;
	if (_pictureCodingType == H264V2_INTRA)
	{
		_pictureCodingType = H264V2_INTER;
		/// The IDR-frame num is by default 0 unless the previous frame was also an IDR-frame.
		/// In this case, it toggles between 1 and 0. The H.264 standard only requires
		/// consecutive IDR-frames to be different ITU-T Recommendation H.264 (03/2005) page 77.
		if ((tmpLastPicCodingType == H264V2_INTRA) && (_idrFrameNum == 0))
			_idrFrameNum = 1;
		else
			_idrFrameNum = 0;
	}//end if H264V2_INTRA...
	else
		_idrFrameNum = 0;

	///< Increment the frame number relative to the last I-frame for the next frame.
	_frameNum = (_frameNum + 1) % _maxFrameNum;

#ifdef H264V2_DUMP_TIMING
	if (_timingTablePos < _timingTableLen)
	{
		_timingTable.WriteItem(0, _timingTablePos, _timingTablePos);
		_timingTable.WriteItem(1, _timingTablePos, (int)GetCounter() - _startTime);
		_timingTablePos++;
	}//end if _timingTablePos...
#endif

	return(1);
}//end Code.

/** Decode the compresed frame into raw pel samples.
The input types are a compressed picture IDR or P NAL unit, a SPS, a PPS or a concatenated
SPS, PPS and compressed picture. The output is the raw picture pels in the format specified
by the "outcolour" codec parameter.
@param  pCmp      : Compressed stream.
@param  bitLength : Length in bits of pCmp.
@param  pDst      : Raw pel output.
@return           : 1 = success, 0 = failure.
*/
int	H264v2Codec::Decode(void* pCmp, int bitLength, void* pDst)
{
	int	runOutOfBits = 0;
	int frameBitSize = bitLength;
	int bitsUsed = 0;
	int ret = 1;
	int moreNonPicNALUnits = 1;

	/// Set the bit stream access. The bit stream reader and related objects are instantiated within 
	/// Open() and is therefore not available for non-picture NAL types. They are temporarily created 
	/// for these other NAL types.
	_bitStreamSize = bitLength;
	if (!_codecIsOpen)
	{
		_pBitStreamReader = new BitStreamReaderMSB();
		_pHeaderUnsignedVlcDec = new ExpGolombUnsignedVlcDecoder();
		_pHeaderSignedVlcDec = new ExpGolombSignedVlcDecoder();
		if ((_pBitStreamReader == NULL) || (_pHeaderUnsignedVlcDec == NULL) || (_pHeaderSignedVlcDec == NULL))
		{
			_errorStr = "[H264Codec::Decode] Cannot instantiate stream reader and vlc objects";
			ret = 0;
			goto H264V2_D_CLEAN_MEM;
		}//end if !_pBitStreamReader...
	}//end if !_codecIsOpen...
	  /// Set the stream reader.
	_pBitStreamReader->SetStream(pCmp, bitLength);

	/// Typically in-band SPS and PPS are prepended to IDR frames. Therefore keep decoding until all
	/// non-picture NAL types are decoded. If the frameBitSize indicates at least 4 more bytes (32 bits)
	/// then we assume there is another NAL to be decoded.
	while (moreNonPicNALUnits)
	{
		/// Extract the start code 0x00000001 from the stream.
		int startCode = _pBitStreamReader->Read(32);
		if (startCode != 1)
		{
			_errorStr = "[H264Codec::Decode] Cannot extract start code from stream";
			ret = 0;
			goto H264V2_D_CLEAN_MEM;
		}//end if frameBitSize...
		frameBitSize -= 32;

		/// Get the NAL header encodings off the bit stream to determine the picture coding type..
		runOutOfBits = ReadNALHeader(_pBitStreamReader, frameBitSize, &bitsUsed);
		frameBitSize -= bitsUsed;
		if (runOutOfBits > 0) ///< An error has occurred. 1 = run out of bits, 2 = vlc decode error.
		{
			ret = 0;
			goto H264V2_D_CLEAN_MEM;
		}//end if runOutOfBits...

		/// NAL to coding type conversion.
		switch (_nal._unit_type)
		{
		case NalHeaderH264::IDR_Slice:
			_pictureCodingType = H264V2_INTRA;
			moreNonPicNALUnits = 0;
			break;
		case NalHeaderH264::NonIDR_NoPartition_Slice:
			/// Slice without partitioning and therefore only one set of slice parameters and NAL type = Slice type.
			_pictureCodingType = H264V2_INTER;
			moreNonPicNALUnits = 0;
			break;
		case NalHeaderH264::SeqParamSet:
		case NalHeaderH264::PicParamSet:
		{
			int changed = 1;  ///< Indicate if the decoded params for this index have changed. Assume they have changed as the default.
			if (_nal._unit_type == NalHeaderH264::SeqParamSet)
			{
				_pictureCodingType = H264V2_SEQ_PARAM;
				/// Read the seq param set off the stream.
				runOutOfBits = ReadSeqParamSet(_pBitStreamReader, frameBitSize, &bitsUsed, &_currSeqParam, &changed);
			}//end if SeqParamSet...
			else ///< if(_nal._unit_type == NalHeaderH264::PicParamSet)
			{
				_pictureCodingType = H264V2_PIC_PARAM;
				/// Read the pic param set off the stream.
				runOutOfBits = ReadPicParamSet(_pBitStreamReader, frameBitSize, &bitsUsed, &_currPicParam, &changed);
				if (!runOutOfBits)
					_currSeqParam = _picParam[_currPicParam]._seq_parameter_set_id;
			}//end else...
			frameBitSize -= bitsUsed;
			if (runOutOfBits > 0) ///< An error has occurred. 1 = run out of bits, 2 = vlc decode error.
			{
				ret = 0;
				goto H264V2_D_CLEAN_MEM;
			}//end if runOutOfBits...

			/// Read the trailing bits off the stream.
			runOutOfBits = ReadTrailingBits(_pBitStreamReader, frameBitSize, &bitsUsed);
			frameBitSize -= bitsUsed;
			if (runOutOfBits > 0) ///< An error has occurred. 1 = run out of bits, 2 = vlc decode error.
			{
				ret = 0;
				goto H264V2_D_CLEAN_MEM;
			}//end if runOutOfBits...

	/// If the codec is in the open state then any change in sequence or picture param set requires the
	/// codec to be restarted with these new sets. But the bit stream must be preserved and restored 
	/// after the call to Open().
			if (_codecIsOpen)
			{
				if (changed)
				{
					/// Preserve the bitstream
					IBitStreamReader* pTmpBitStreamReader = new BitStreamReaderMSB();
					pTmpBitStreamReader->Copy(_pBitStreamReader);

					int tmpPicCodingType = _pictureCodingType; ///< Store the coding type.

					_genParamSetOnOpen = 0; ///< Must be off if the new param sets are to be used.
					if (!Open())
					{
						delete pTmpBitStreamReader;
						ret = 0;
						goto H264V2_D_CLEAN_MEM;
					}//end if !Open...

					/// Restore the picture coding type.
					_pictureCodingType = tmpPicCodingType;
					/// Restore the bitstream.
					_pBitStreamReader->Copy(pTmpBitStreamReader);
					delete pTmpBitStreamReader;
				}//end if changed...

			}//end if _codecIsOpen...

#ifdef H264V2_DUMP_HEADERS
			if (_headerTablePos < _headerTableLen)
			{
				_headerTable.WriteItem(0, _headerTablePos, _nal._unit_type);     /// "NALType"
				_headerTable.WriteItem(1, _headerTablePos, _nal._ref_idc);       /// "NALRefIdc"
				_headerTable.WriteItem(2, _headerTablePos, _slice._idr_pic_id);  ///  "IdrPicId"
				_headerTable.WriteItem(3, _headerTablePos, _slice._frame_num);   ///  "FrmNum"
				_headerTable.WriteItem(4, _headerTablePos, _slice._type);        ///  "SliceType"
				_headerTable.WriteItem(5, _headerTablePos, _slice._qp);          ///  "Qp"

				_headerTablePos++;
			}//end if _headerTablePos...
#endif // H264V2_DUMP_HEADERS

			if (frameBitSize < 32) ///< No more units.
			{
				moreNonPicNALUnits = 0;
				if (_codecIsOpen)
					return(1);
				else
				{
					/// Clean up the mem.
					ret = 1;
					goto H264V2_D_CLEAN_MEM;
				}//end else...
			}//end if frameBitSize...

		};//end SeqParamSet and PicParamSet block...
		break;
		default:
			_errorStr = "[H264v2Codec::Decode] NAL unit type not supported";
			ret = 0;
			goto H264V2_D_CLEAN_MEM;
			break;
		}//end switch _unit_type...
	}//end while moreNonPicNALUnits...

	  /// Only IDR and P pictures are decoded from here and the codec must be open.
	if (!_codecIsOpen)
	{
		_errorStr = "[H264v2Codec::Decode] Codec is not open";
		ret = 0;
		goto H264V2_D_CLEAN_MEM;
	}//end if !_codecIsOpen...

	  /// Only baseline profile is supported: _profile_idc = 66.
	if (_seqParam[_currSeqParam]._profile_idc != 66)
	{
		_errorStr = "[H264Codec::Decode] This implementation only supports the baseline profile";
		return(0);
	}//end if _profile_idc not baseline...

  /// Remove prevention of start code emulation codes within the coded bit stream.
	if (_startCodeEmulationPrevention)
		frameBitSize -= RemoveEmulationPrevention(_pBitStreamReader);

	/// Get the slice header encodings off the bit stream. As this implementation 
	/// has only one slice, the slice header, slice data (macroblocks) and the 
	/// slice trailing bits can be decoded in linear order.
	runOutOfBits = ReadSliceLayerHeader(_pBitStreamReader, frameBitSize, &bitsUsed);
	frameBitSize -= bitsUsed;
	if (runOutOfBits > 0) ///< An error has occurred. 1 = run out of bits, 2 = vlc decode error.
		return(0);
	/// Load frame counter members from the decoded slice header.
	_frameNum = _slice._frame_num;
	_idrFrameNum = _slice._idr_pic_id;
	/// Load the picture and sequence parameter set references.
	_currPicParam = _slice._pic_parameter_set_id;
	_currSeqParam = _picParam[_currPicParam]._seq_parameter_set_id;
	/// There is only one slice so the quant parameter is picture quant + the delta slice quant.
	_slice._qp = _picParam[_currPicParam]._pic_init_qp_minus26 + 26 + _slice._qp_delta;
	_pQuant = _slice._qp;

#ifdef H264V2_DUMP_HEADERS
	if (_headerTablePos < _headerTableLen)
	{
		_headerTable.WriteItem(0, _headerTablePos, _nal._unit_type);     /// "NALType"
		_headerTable.WriteItem(1, _headerTablePos, _nal._ref_idc);       /// "NALRefIdc"
		_headerTable.WriteItem(2, _headerTablePos, _slice._idr_pic_id);  ///  "IdrPicId"
		_headerTable.WriteItem(3, _headerTablePos, _slice._frame_num);   ///  "FrmNum"
		_headerTable.WriteItem(4, _headerTablePos, _slice._type);        ///  "SliceType"
		_headerTable.WriteItem(5, _headerTablePos, _slice._qp);          ///  "Qp"

		_headerTablePos++;
	}//end if _headerTablePos...
#endif // H264V2_DUMP_HEADERS

	/// Get the macroblock (slice data) encodings off the bit stream.
	runOutOfBits = ReadSliceDataLayer(_pBitStreamReader, frameBitSize, &bitsUsed);
	frameBitSize -= bitsUsed;
	if (runOutOfBits > 0) ///< An error has occurred. 1 = run out of bits, 2 = vlc decode error.
		return(0);

	/// Get the slice trailing bits off the bit stream. 
	runOutOfBits = ReadTrailingBits(_pBitStreamReader, frameBitSize, &bitsUsed);
	frameBitSize -= bitsUsed;
	if (runOutOfBits > 0) ///< An error has occurred. 1 = run out of bits, 2 = vlc decode error.
		return(0);

	/// INTRA frames require the reference images to be zeroed.
	if (_pictureCodingType == H264V2_INTRA)
	{
		Restart();	///< Reset the loop and ref img.

			/// The decoder was chosen in Open() depending on the mode selected. It
			/// operates on the full list of macroblocks.
		if (!_pIntraImgPlaneDecoder->Decode())
			return(0);	///< An error has occured.
	}//end if INTRA...
	else
	{
		/// INTER picture decode.
		/// The decoder was chosen in Open() depending on the mode selected. It
		/// operates on the list of macroblocks. Motion compensation is included.
		if (!_pInterImgPlaneDecoder->Decode())
			return(0);	///< An error has occured.
	}//end else INTER...

	/// In-loop filter for 4x4 block boundaries to remove blocking artefacts.
	if (_slice._disable_deblocking_filter_idc != 1)
		ApplyLoopFilter();

	/// Convert to the output image depending on the output dimension settings. Set
	  /// up in the Open() method for the correctly selected converter.
	if (_outColour == H264V2_YUV420P16)      /// The natural colour space of the decoder with type = short.
		memcpy((void *)pDst, (const void *)_pLum, ((_lumWidth * _lumHeight) + 2 * (_chrWidth * _chrHeight)) * sizeof(short));
	else if (_outColour == H264V2_YUV420P8)  ///< ...type = byte.
	{
		int colLen = (_lumWidth * _lumHeight) + 2 * (_chrWidth * _chrHeight);
		for (int i = 0; i < colLen; i++)
			((unsigned char *)pDst)[i] = (unsigned char)_pLum[i];
	}//end if H264V2_YUV420P8...
	else
		_pOutColourConverter->Convert(_pRLum, _pRChrU, _pRChrV, pDst);

	return(1);

	/// Clean up memory objects.
H264V2_D_CLEAN_MEM:
	if (_pHeaderUnsignedVlcDec != NULL)
	{
		delete _pHeaderUnsignedVlcDec;
		_pHeaderUnsignedVlcDec = NULL;
	}//end if _pHeaderUnsignedVlcDec...
	if (_pHeaderSignedVlcDec != NULL)
	{
		delete _pHeaderSignedVlcDec;
		_pHeaderSignedVlcDec = NULL;
	}//end if _pHeaderSignedVlcDec...
	if (_pBitStreamReader != NULL)
	{
		delete _pBitStreamReader;
		_pBitStreamReader = NULL;
	}//end if _pBitStreamReader...

	return(ret);
}//end Decode.

int H264v2Codec::Close(void)
{
#ifdef H264V2_DUMP_HEADERS
	if (_headerTablePos > 0)
		_headerTable.Save("c:/keithf/CppProjects/RTVC/Projects/Win32/VC10/CodecAnalyser/LoyisoGola_my_enc_qp48_16P.csv", ",", 1);

	_headerTablePos = 0;
#endif // H264V2_DUMP_HEADERS
#ifdef H264V2_DUMP_MB_RD_DATA
	if (_mbRDTablePos > 1)
		_mbRDTable.Save("c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/QPvRD_Foreman.csv", ",", 1);

	_mbRDTablePos = 1;
#endif
#ifdef H264V2_DUMP_TIMING
	if (_timingTablePos > 0)
		_timingTable.Save("c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/EncodeTimes.csv", ",", 1);

	_timingTablePos = 0;
#endif
#ifdef H264V2_DUMP_FRM_RD_DATA
	/// Collect frame RD values.
	_frmRDTableLen = _pRateCntlPFrames->GetFameBufferLength();
	_frmRDTable.Create(4, _frmRDTableLen);  /// frame, mb Dmax, rate, MAD.

	_frmRDTable.SetHeading(0, "Frame");
	_frmRDTable.SetDataType(0, MeasurementTable::INT);
	_frmRDTable.SetHeading(1, "Dmax");
	_frmRDTable.SetDataType(1, MeasurementTable::INT);
	_frmRDTable.SetHeading(2, "Rate");
	_frmRDTable.SetDataType(2, MeasurementTable::DOUBLE);
	_frmRDTable.SetHeading(3, "MAD");
	_frmRDTable.SetDataType(3, MeasurementTable::DOUBLE);

	_frmRDTablePos = 0;

	if (_pRateCntlPFrames->ValidData())
	{
		int frmNum = _frmRDTableLen - 1; ///< Frame numbering is in reverse order.
		for (_frmRDTablePos = 0; _frmRDTablePos < _frmRDTableLen; _frmRDTablePos++, frmNum--)
		{
			_frmRDTable.WriteItem(0, _frmRDTablePos, _frmRDTablePos); ///< Frame counter.
			_frmRDTable.WriteItem(1, _frmRDTablePos, (int)(0.5 + (_pRateCntlPFrames->GetDmaxSamples())[frmNum]));
			_frmRDTable.WriteItem(2, _frmRDTablePos, (_pRateCntlPFrames->GetRateSamples())[frmNum]); ///< In bits/pixel
			_frmRDTable.WriteItem(3, _frmRDTablePos, (_pRateCntlPFrames->GetMDSamples())[frmNum]);
		}//end for _frmRDTablePos...

		_frmRDTable.Save("c:/keithf/CppProjects/RTVC/Projects/Win32/VC11/CodecAnalyser/DmaxVsR_Foreman.csv", ",", 1);
		_frmRDTablePos = 0;
	}//end if ValidData...

#endif

	/// Free the image memory and associated overlays.
	if (_Lum != NULL)
		delete _Lum;
	_Lum = NULL;
	if (_Cb != NULL)
		delete _Cb;
	_Cb = NULL;
	if (_Cr != NULL)
		delete _Cr;
	_Cr = NULL;

	if (_RefLum != NULL)
		delete _RefLum;
	_RefLum = NULL;
	if (_RefCb != NULL)
		delete _RefCb;
	_RefCb = NULL;
	if (_RefCr != NULL)
		delete _RefCr;
	_RefCr = NULL;

	if (_pLum != NULL)
		delete[] _pLum;
	_pLum = NULL;
	_pChrU = NULL;
	_pChrV = NULL;
	_pRLum = NULL;
	_pRChrU = NULL;
	_pRChrV = NULL;

	if (_16x16 != NULL)
		delete _16x16;
	_16x16 = NULL;
	if (_p16x16 != NULL)
		delete[] _p16x16;
	_p16x16 = NULL;
	if (_8x8_0 != NULL)
		delete _8x8_0;
	_8x8_0 = NULL;
	if (_p8x8_0 != NULL)
		delete[] _p8x8_0;
	_p8x8_0 = NULL;
	if (_8x8_1 != NULL)
		delete _8x8_1;
	_8x8_1 = NULL;
	if (_p8x8_1 != NULL)
		delete[] _p8x8_1;
	_p8x8_1 = NULL;

	if (_mbImg != NULL)
		delete _mbImg;
	_mbImg = NULL;

	/// Colour converters.
	if (_pInColourConverter != NULL)
		delete _pInColourConverter;
	_pInColourConverter = NULL;

	if (_pOutColourConverter != NULL)
		delete _pOutColourConverter;
	_pOutColourConverter = NULL;

	/// IT transform filters.
	if (_pF4x4TLum != NULL)
		delete _pF4x4TLum;
	_pF4x4TLum = NULL;
	if (_pF4x4TChr != NULL)
		delete _pF4x4TChr;
	_pF4x4TChr = NULL;
	if (_pFDC4x4T != NULL)
		delete _pFDC4x4T;
	_pFDC4x4T = NULL;
	if (_pFDC2x2T != NULL)
		delete _pFDC2x2T;
	_pFDC2x2T = NULL;
	if (_pI4x4TLum != NULL)
		delete _pI4x4TLum;
	_pI4x4TLum = NULL;
	if (_pI4x4TChr != NULL)
		delete _pI4x4TChr;
	_pI4x4TChr = NULL;
	if (_pIDC4x4T != NULL)
		delete _pIDC4x4T;
	_pIDC4x4T = NULL;
	if (_pIDC2x2T != NULL)
		delete _pIDC2x2T;
	_pIDC2x2T = NULL;

	/// Vlc encoders and decoders.
	if (_pPrefixVlcEnc != NULL)
		delete _pPrefixVlcEnc;
	_pPrefixVlcEnc = NULL;
	if (_pPrefixVlcDec != NULL)
		delete _pPrefixVlcDec;
	_pPrefixVlcDec = NULL;

	if (_pCoeffTokenVlcEnc != NULL)
		delete _pCoeffTokenVlcEnc;
	_pCoeffTokenVlcEnc = NULL;
	if (_pCoeffTokenVlcDec != NULL)
		delete _pCoeffTokenVlcDec;
	_pCoeffTokenVlcDec = NULL;

	if (_pTotalZeros4x4VlcEnc != NULL)
		delete _pTotalZeros4x4VlcEnc;
	_pTotalZeros4x4VlcEnc = NULL;
	if (_pTotalZeros4x4VlcDec != NULL)
		delete _pTotalZeros4x4VlcDec;
	_pTotalZeros4x4VlcDec = NULL;

	if (_pTotalZeros2x2VlcEnc != NULL)
		delete _pTotalZeros2x2VlcEnc;
	_pTotalZeros2x2VlcEnc = NULL;
	if (_pTotalZeros2x2VlcDec != NULL)
		delete _pTotalZeros2x2VlcDec;
	_pTotalZeros2x2VlcDec = NULL;

	if (_pRunBeforeVlcEnc != NULL)
		delete _pRunBeforeVlcEnc;
	_pRunBeforeVlcEnc = NULL;
	if (_pRunBeforeVlcDec != NULL)
		delete _pRunBeforeVlcDec;
	_pRunBeforeVlcDec = NULL;

	if (_pBlkPattVlcEnc != NULL)
		delete _pBlkPattVlcEnc;
	_pBlkPattVlcEnc = NULL;
	if (_pBlkPattVlcDec != NULL)
		delete _pBlkPattVlcDec;
	_pBlkPattVlcDec = NULL;

	if (_pDeltaQPVlcEnc != NULL)
		delete _pDeltaQPVlcEnc;
	_pDeltaQPVlcEnc = NULL;
	if (_pDeltaQPVlcDec != NULL)
		delete _pDeltaQPVlcDec;
	_pDeltaQPVlcDec = NULL;

	if (_pMbTypeVlcEnc != NULL)
		delete _pMbTypeVlcEnc;
	_pMbTypeVlcEnc = NULL;
	if (_pMbTypeVlcDec != NULL)
		delete _pMbTypeVlcDec;
	_pMbTypeVlcDec = NULL;

	/// Vlc codecs that were used as references and not instantiated.
	_pMbIChrPredModeVlcEnc = NULL;
	_pMbIChrPredModeVlcDec = NULL;
	_pMbMotionVecDiffVlcEnc = NULL;
	_pMbMotionVecDiffVlcDec = NULL;
	_pHeaderUnsignedVlcEnc = NULL;
	_pHeaderUnsignedVlcDec = NULL;
	_pHeaderSignedVlcEnc = NULL;
	_pHeaderSignedVlcDec = NULL;

	/// The CAVLC codecs.
	if (_pCAVLC4x4 != NULL)
		delete _pCAVLC4x4;
	_pCAVLC4x4 = NULL;
	if (_pCAVLC2x2 != NULL)
		delete _pCAVLC2x2;
	_pCAVLC2x2 = NULL;

	/// Macroblock data objects.
	if (_pMb != NULL)
		delete[] _pMb;
	_pMb = NULL;
	if (_Mb != NULL)
		delete[] _Mb;
	_Mb = NULL;

	if (_autoIFrameIncluded != NULL)
		delete[] _autoIFrameIncluded;
	_autoIFrameIncluded = NULL;

	/// Stream access.
	if (_pBitStreamWriter != NULL)
		delete _pBitStreamWriter;
	_pBitStreamWriter = NULL;

	if (_pBitStreamReader != NULL)
		delete _pBitStreamReader;
	_pBitStreamReader = NULL;

	/// Motion estimator and compensators.
	if (_pMotionEstimator != NULL)
		delete _pMotionEstimator;
	_pMotionEstimator = NULL;

	/// Motion compensation vectors.
	if (_pMotionVectors != NULL)
		delete _pMotionVectors;
	_pMotionVectors = NULL;

	if (_pMotionCompensator != NULL)
		delete _pMotionCompensator;
	_pMotionCompensator = NULL;

	if (_pMotionPredictor != NULL)
		delete _pMotionPredictor;
	_pMotionPredictor = NULL;

	/// Image plane encoders/decoders.
	if (_pIntraImgPlaneEncoder != NULL)
		delete _pIntraImgPlaneEncoder;
	_pIntraImgPlaneEncoder = NULL;

	if (_pInterImgPlaneEncoder != NULL)
		delete _pInterImgPlaneEncoder;
	_pInterImgPlaneEncoder = NULL;

	if (_pIntraImgPlaneDecoder != NULL)
		delete _pIntraImgPlaneDecoder;
	_pIntraImgPlaneDecoder = NULL;

	if (_pInterImgPlaneDecoder != NULL)
		delete _pInterImgPlaneDecoder;
	_pInterImgPlaneDecoder = NULL;

	/// Rate controllers.
	if (_pRateCntlPFrames != NULL)
	{
//		_pRateCntlPFrames->Dump(H264V2_RATE_CNTL_PFRAMES);
		delete _pRateCntlPFrames;
		_pRateCntlPFrames = NULL;
	}//end if _pRateCntlPFrames...
	if (_pRateCntlIFrames != NULL)
	{
//		_pRateCntlIFrames->Dump(H264V2_RATE_CNTL_IFRAMES);
		delete _pRateCntlIFrames;
		_pRateCntlIFrames = NULL;
	}//end if _pRateCntlIFrames...

  /// Region of Interest encoding.
  if (_roiMultiplier != NULL)
    delete[] _roiMultiplier;
  _roiMultiplier = NULL;

	_codecIsOpen = 0;
	return(1);
}//end Close.

void H264v2Codec::Restart(void)
{
	_pictureCodingType = H264V2_INTRA;
	_frameNum = 0;				///< Reset the frame counter.
  /// _idrFrameNum does not require reseting.

  /// Zero the reference image. Note that the colour components are 
	/// held in contiguous mem.
	int imgSize = (_lumWidth * _lumHeight) + 2 * (_chrWidth * _chrHeight);
	memset(_pRLum, 0, imgSize * sizeof(short));

}//end Restart.

/*
-----------------------------------------------------------------------
  Private Implementation.
-----------------------------------------------------------------------
*/
/** Code non-picture nal types.
This method operates independently and therefore all the coding objects must be
instantiated and destroyed before and after the coding process. This is typically an
out-of-band process and therefore speed is not critical. The codec must NOT be open
for calls to this method i.e. _codecIsOpen = 0.
@param pCmp						: The stream memory to write to.
@param frameBitLimit	: The max number of bits to use.
@return								: 1 = success, 0 = failure.
*/
int H264v2Codec::CodeNonPicNALTypes(void* pCmp, int frameBitLimit)
{
	int allowedBits, bitsUsed;

	int runOutOfBits = 0;
	int bitLimit = frameBitLimit;
	int ret = 1;	///< Return value default to success.
	_bitStreamSize = 0;

	/// This method uses the class members and therefore Open() must not be called before
	/// coding non-pic nal-types.
	if (_codecIsOpen)
	{
		_errorStr = "[H264V2::CodeNonPicNALTypes] Codec must not be open for non-picture nal types";
		return(0);
	}//end if _codecIsOpen...

	  /// Check which types are supported with this implementation.
	if ((_pictureCodingType != H264V2_SEQ_PARAM) && (_pictureCodingType != H264V2_PIC_PARAM))
	{
		_errorStr = "[H264Codec::CodeNonPicNALTypes] Non-Nal picture type not supported";
		return(0);
	}//end if !H264V2_SEQ_PARAM...

	///----------------------------------------------------------------------------------------------
	/// Instantiate the mem objects required for the encoding.

	/// A stream writer.
	_pBitStreamWriter = new BitStreamWriterMSB();
	if (_pBitStreamWriter == NULL)
	{
		_errorStr = "[H264Codec::CodeNonPicNALTypes] Cannot instantiate bit stream writer object";
		ret = 0;
		goto H264V2_CNPNT_CLEAN_MEM;
	}//end if !_pBitStreamWriter...
	/// Set the stream writer to the compressed input parameter reference.
	_pBitStreamWriter->SetStream(pCmp, frameBitLimit);

	/// Vlc encoder and decoder for general headers. ExpGolomb codecs
	/// are stateless therefore they can be reused.
	_pHeaderUnsignedVlcEnc = new ExpGolombUnsignedVlcEncoder();
	_pHeaderSignedVlcEnc = new ExpGolombSignedVlcEncoder();
	if ((_pHeaderUnsignedVlcEnc == NULL) || (_pHeaderSignedVlcEnc == NULL))
	{
		_errorStr = "[H264Codec::CodeNonPicNALTypes] Cannot instantiate vlc encoder objects";
		ret = 0;
		goto H264V2_CNPNT_CLEAN_MEM;
	}//end if _pHeaderUnsignedVlcEnc...

	///----------------------------------------------------------------------------------------------
	/// Parameter set coding. The _currSeqParam and _currPicParam members determine the param sets
	/// to encode. From here the picture coding type is only H264V2_SEQ_PARAM or H264V2_PIC_PARAM.

	_nal._ref_idc = 3;

	if (_pictureCodingType == H264V2_SEQ_PARAM)
	{
		_nal._unit_type = NalHeaderH264::SeqParamSet;
		/// Sequence parameter set for Baseline profile.
		if (!SetSeqParamSet(_currSeqParam))
		{
			_errorStr = "[H264Codec::CodeNonPicNALTypes] Cannot set sequence parameter set";
			ret = 0;
			goto H264V2_CNPNT_CLEAN_MEM;
		}//end if !SetSeqParamSet...
	}//end if H264V2_SEQ_PARAM...
	else ///< if(_pictureCodingType == H264V2_PIC_PARAM)
	{
		_nal._unit_type = NalHeaderH264::PicParamSet;
		/// Picture parameter set.
		if (!SetPicParamSet(_currPicParam, _currSeqParam))
		{
			_errorStr = "[H264Codec::CodeNonPicNALTypes] Cannot set picture parameter set";
			ret = 0;
			goto H264V2_CNPNT_CLEAN_MEM;
		}//end if !SetPicParamSet...
	}//end else...

  /// Write the 32-bit start code 0x00000001 to the stream.
	allowedBits = bitLimit - _bitStreamSize;
	if (allowedBits < 32)
	{
		_errorStr = "[H264V2Codec::CodeNonPicNALTypes] Cannot write start code to stream";
		ret = 0;
		goto H264V2_CNPNT_CLEAN_MEM;
	}//end if allowedBits...
	_pBitStreamWriter->Write(32, 1);
	_bitStreamSize += 32;

	/// Write the NAL header to the stream. 
	allowedBits = bitLimit - _bitStreamSize;
	runOutOfBits = WriteNALHeader(_pBitStreamWriter, allowedBits, &bitsUsed);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
	{
		ret = 0;
		goto H264V2_CNPNT_CLEAN_MEM;
	}//end if runOutOfBits...

	/// Write the parameter set to the stream.
	allowedBits = bitLimit - _bitStreamSize;
	if (_pictureCodingType == H264V2_SEQ_PARAM)
		runOutOfBits = WriteSeqParamSet(_pBitStreamWriter, allowedBits, &bitsUsed, _currSeqParam);
	else ///< if(_pictureCodingType == H264V2_PIC_PARAM)
		runOutOfBits = WritePicParamSet(_pBitStreamWriter, allowedBits, &bitsUsed, _currPicParam);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
	{
		ret = 0;
		goto H264V2_CNPNT_CLEAN_MEM;
	}//end if runOutOfBits...

	/// Write trailing bits to the stream.
	allowedBits = bitLimit - _bitStreamSize;
	runOutOfBits = WriteTrailingBits(_pBitStreamWriter, allowedBits, &bitsUsed);
	_bitStreamSize += bitsUsed;
	if (runOutOfBits) ///< or if(== 2) An error has occured.
	{
		ret = 0;
		goto H264V2_CNPNT_CLEAN_MEM;
	}//end if runOutOfBits...

	///----------------------------------------------------------------------------------------------
	/// Clean up memory objects.
H264V2_CNPNT_CLEAN_MEM:
	if (_pHeaderUnsignedVlcEnc != NULL)
	{
		delete _pHeaderUnsignedVlcEnc;
		_pHeaderUnsignedVlcEnc = NULL;
	}//end if _pHeaderUnsignedVlcEnc...
	if (_pHeaderSignedVlcEnc != NULL)
	{
		delete _pHeaderSignedVlcEnc;
		_pHeaderSignedVlcEnc = NULL;
	}//end if _pHeaderSignedVlcEnc...
	if (_pBitStreamWriter != NULL)
	{
		delete _pBitStreamWriter;
		_pBitStreamWriter = NULL;
	}//end if _pBitStreamWriter...

	return(ret);
}//end CodeNonPicNALTypes.

/** Set the Sequence parameter set.
Set one of the seq param sets defined by the index parameter where the
values are determined by interpreting the codec parameters.
@param index	:	Seq parameter set to modify [0..31].
@return				: Success = 1, failure = 0.
*/
int H264v2Codec::SetSeqParamSet(int index)
{
	if (index > 31)
		return(0);

	_seqParam[index]._seq_parameter_set_id = index;					///< The index for this definition of sequence params [0..31].
	_seqParam[index]._profile_idc = 66;							///< Baseline profile = 66.
	_seqParam[index]._constraint_set0_flag = 0;							///< = 0.
	_seqParam[index]._constraint_set1_flag = 0;							///< = 0.
	_seqParam[index]._constraint_set2_flag = 0;							///< = 0.
	_seqParam[index]._constraint_set3_flag = 0;							///< = 0.
	_seqParam[index]._level_idc = 20;							///< = 20 (Level num = 2, calculated as 10 x level num).
	_seqParam[index]._chroma_format_idc = 1;							///< Range = [0..3]. When not present = 1 (4:2:0).
	_seqParam[index]._residual_colour_transform_flag = 0;							///< = 0.
	_seqParam[index]._bit_depth_luma_minus8 = 0;							///< Range = [0..4]. When not present = 0 (8 bits/lum component).
	_seqParam[index]._bit_depth_chroma_minus8 = 0;							///< Range = [0..4]. When not present = 0 (8 bits/chr component).
	_seqParam[index]._qpprime_y_zero_transform_bypass_flag = 0;							///< If QP = 0, this flag indicates bypassing the transform process. When not present = 0.
	_seqParam[index]._seq_scaling_matrix_present_flag = 0;							///< = 0. No scaling matrices for baseline profile. Indicates existence of _seq_scaling_list_present_flag flags.
	/// Not initialised _seq_scaling_list_present_flag[8]			///< if _seq_scaling_matrix_present_flag, these indicate lists not present/present (0/1).
	_seqParam[index]._log2_max_frame_num_minus4 = _seqParamSetLog2MaxFrameNumMinus4;	///< Range = [0..12]. MaxFrameNum = 2^(_log2_max_frame_num_minus4 + 4) for slice _frame_num derivations.
	_seqParam[index]._pic_order_cnt_type = 2;							///< Range = [0..2]. 2 = 1-to-1 mapping. (Picture order count = frame num).
	_seqParam[index]._log2_max_pic_order_cnt_lsb_minus4 = 0;							///< For B-Slice decoding.  There are no B-Slices in the baseline profile. 
	_seqParam[index]._delta_pic_order_always_zero_flag = 0;							///< For B-Slice decoding.
	_seqParam[index]._offset_for_non_ref_pic = 0;							///< For B-Slice decoding.
	_seqParam[index]._offset_for_top_to_bottom_field = 0;							///< For B-Slice decoding.
	_seqParam[index]._num_ref_frames_in_pic_order_cnt_cycle = 0;							///< For B-Slice decoding.
	/// Not initialised _offset_for_ref_frame[256];						///< For B-Slice decoding.
	_seqParam[index]._num_ref_frames = 1;							///< Max num of short-term and long-term ref frames.
	_seqParam[index]._gaps_in_frame_num_value_allowed_flag = 0;							///< Indicates allowed values of slice _frame_num.
	_seqParam[index]._frame_mbs_only_flag = 1;							///< = 1 (baseline profile). fields/frames = 0/1.
	_seqParam[index]._mb_adaptive_frame_field_flag = 0;							///< Indicates switching between frame and field macroblocks. When not present = 0.
	_seqParam[index]._direct_8x8_inference_flag = 0;							///< Methods for B-Slice motion vector decoding. There are no B-Slices in the baseline profile.
	_seqParam[index]._frame_cropping_flag = 0;							///< Indicates that the cropping params below follow in the stream.
	_seqParam[index]._frame_crop_left_offset = 0;							///< In frame coordinate units.
	_seqParam[index]._frame_crop_right_offset = 0;							///< In frame coordinate units.
	_seqParam[index]._frame_crop_top_offset = 0;							///< In frame coordinate units.
	_seqParam[index]._frame_crop_bottom_offset = 0;							///< In frame coordinate units.
	_seqParam[index]._vui_parameters_present_flag = 0;							///< vui parameters follow.

	if ((_width < 16) || (_height < 16))
		return(0);

	_seqParam[index]._pic_width_in_mbs_minus1 = (_width / 16) - 1;	///< Width of decoded pic in units of macroblocks. Derive lum and chr pic width from this variable.
	_seqParam[index]._pic_height_in_map_units_minus1 = (_height / 16) - 1;	///< Map units are 2 macroblocks for fields and 1 for frames. Derive lum and chr pic height from this variable.

	return(1);
}//end SetSeqParamSet.

/** Write the sequence parameter set to a bit stream.
The seq param set must be correctly defined before this method is called. The
vlc encoding is performed first before writing. Each write checks if a bit
overflow will occur before writing. The check is sufficiently frequent to
warrant an early exit GOTO statement. If the input stream param is NULL then
this method is used to count the bits only.
@param bsw					: Stream to write into.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@param index				: Sequence parameter set index to write.
@return							: Run out of bits = 1, more bits available = 0, Vlc error = 2, unsupported parameter set = 3.
*/
int H264v2Codec::WriteSeqParamSet(IBitStreamWriter* bsw, int allowedBits, int* bitsUsed, int index)
{
	int	bitCount, i;
	int bitsUsedSoFar = 0;

	/// Only baseline profile is supported: _profile_idc = 66.
//	if(_seqParam[index]._profile_idc != 66)
//	{
//		_errorStr = "H264V2:[H264Codec::WriteSeqParamSet] This implementation only supports the baseline profile";
//		*bitsUsed = 0;
//		return(3);
//	}//end if _profile_idc not baseline...

	/// The first set of bit fields are fixed with a total of 24 bits.
	if (allowedBits < 24)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
	{
		bsw->Write(8, _seqParam[index]._profile_idc);						///< u(8)
		bsw->Write(_seqParam[index]._constraint_set0_flag);			///< u(1)
		bsw->Write(_seqParam[index]._constraint_set1_flag);			///< u(1)
		bsw->Write(_seqParam[index]._constraint_set2_flag);			///< u(1)
		bsw->Write(_seqParam[index]._constraint_set3_flag);			///< u(1)
		bsw->Write(4, 0);																				///< u(4), reserved_zero_4bits.
		bsw->Write(8, _seqParam[index]._level_idc);							///< u(8)
	}//end if bsw...
	bitsUsedSoFar += 24;

	/// _seq_parameter_set_id
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._seq_parameter_set_id);  ///< ue(v)
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WSPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	if ((_seqParam[index]._profile_idc == 100) || (_seqParam[index]._profile_idc == 110) ||
		(_seqParam[index]._profile_idc == 122) || (_seqParam[index]._profile_idc == 144))
	{
		/// _chroma_format_idc
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._chroma_format_idc);  ///< ue(v)
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

		/// _residual_colour_transform_flag
		if (_seqParam[index]._chroma_format_idc == 3)
		{
			if (bsw)
				bsw->Write(_seqParam[index]._residual_colour_transform_flag); ///< u(1)
			bitsUsedSoFar++;
		}///end if _chroma_format_idc...

		/// _bit_depth_luma_minus8
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._bit_depth_luma_minus8);  ///< ue(v)
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

		/// _bit_depth_chroma_minus8
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._bit_depth_chroma_minus8);  ///< ue(v)
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

		/// Two flags
		if (bsw)
		{
			bsw->Write(_seqParam[index]._qpprime_y_zero_transform_bypass_flag); ///< u(1)
			bsw->Write(_seqParam[index]._seq_scaling_matrix_present_flag);      ///< u(1)
		}//end if bsw...
		bitsUsedSoFar += 2;

		/// Scaling matricies
		if (_seqParam[index]._seq_scaling_matrix_present_flag)
		{
			for (i = 0; i < 8; i++)
			{
				/// _seq_scaling_list_present_flag[8]
				if (bsw)
					bsw->Write(_seqParam[index]._seq_scaling_list_present_flag[i]); ///< u(1)
				bitsUsedSoFar++;

				bitCount = 0;
				if (_seqParam[index]._seq_scaling_list_present_flag[i])
				{
					if (i < 6)
						bitCount += WriteSeqParamScalingList(bsw, _seqParam[index]._scaling_list_4x4[i], 16, &(_seqParam[index]._use_default_scaling_matrix_4x4_flag[i]));
					else
						bitCount += WriteSeqParamScalingList(bsw, _seqParam[index]._scaling_list_8x8[i - 6], 64, &(_seqParam[index]._use_default_scaling_matrix_8x8_flag[i - 6]));
				}//end if _seq_scaling_list_present_flag...

				if ((bitsUsedSoFar + bitCount) > allowedBits)
					goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
				bitsUsedSoFar += bitCount;
			}//end for i...
		}//end if _seq_scaling_matrix_present_flag...

	}//end if _profile_idc...

	  /// _log2_max_frame_num_minus4
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._log2_max_frame_num_minus4); ///< ue(v)
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WSPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _pic_order_cnt_type
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._pic_order_cnt_type);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WSPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	if (_seqParam[index]._pic_order_cnt_type == 0)
	{
		/// _log2_max_pic_order_cnt_lsb_minus4
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._log2_max_pic_order_cnt_lsb_minus4);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;
	}//end if _pic_order_cnt_type...
	else if (_seqParam[index]._pic_order_cnt_type == 1)
	{
		/// _delta_pic_order_always_zero_flag
		if ((bitsUsedSoFar + 1) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(_seqParam[index]._delta_pic_order_always_zero_flag);
		bitsUsedSoFar++;

		/// _offset_for_non_ref_pic
		bitCount = _pHeaderSignedVlcEnc->Encode(_seqParam[index]._offset_for_non_ref_pic);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

		/// _offset_for_top_to_bottom_field
		bitCount = _pHeaderSignedVlcEnc->Encode(_seqParam[index]._offset_for_top_to_bottom_field);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

		/// _num_ref_frames_in_pic_order_cnt_cycle
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._num_ref_frames_in_pic_order_cnt_cycle);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

		for (i = 0; i < _seqParam[index]._num_ref_frames_in_pic_order_cnt_cycle; i++)
		{
			/// _offset_for_ref_frame[256]
			bitCount = _pHeaderSignedVlcEnc->Encode(_seqParam[index]._offset_for_ref_frame[i]);
			if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
				goto H264V2_WSPS_VLCERROR_WRITE;
			if ((bitsUsedSoFar + bitCount) > allowedBits)
				goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
			if (bsw)
				bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
			bitsUsedSoFar += bitCount;
		}//end for i...

	}//end else if _pic_order_cnt_type...

	/// _num_ref_frames
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._num_ref_frames);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WSPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _gaps_in_frame_num_value_allowed_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_seqParam[index]._gaps_in_frame_num_value_allowed_flag);
	bitsUsedSoFar++;

	/// _pic_width_in_mbs_minus1
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._pic_width_in_mbs_minus1);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WSPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _pic_height_in_map_units_minus1
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._pic_height_in_map_units_minus1);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WSPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _frame_mbs_only_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_seqParam[index]._frame_mbs_only_flag);
	bitsUsedSoFar++;

	if (!_seqParam[index]._frame_mbs_only_flag)
	{
		/// _mb_adaptive_frame_field_flag
		if ((bitsUsedSoFar + 1) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(_seqParam[index]._mb_adaptive_frame_field_flag);
		bitsUsedSoFar++;
	}//end if !_frame_mbs_only_flag...

	/// _direct_8x8_inference_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_seqParam[index]._direct_8x8_inference_flag);
	bitsUsedSoFar++;

	/// _frame_cropping_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_seqParam[index]._frame_cropping_flag);
	bitsUsedSoFar++;

	if (_seqParam[index]._frame_cropping_flag)
	{
		/// _frame_crop_left_offset
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._frame_crop_left_offset);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;
		/// _frame_crop_right_offset
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._frame_crop_right_offset);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;
		/// _frame_crop_top_offset
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._frame_crop_top_offset);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;
		/// _frame_crop_bottom_offset
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_seqParam[index]._frame_crop_bottom_offset);
		if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
			goto H264V2_WSPS_VLCERROR_WRITE;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
		if (bsw)
			bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;
	}//end if _frame_cropping_flag...

	/// _vui_parameters_present_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WSPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_seqParam[index]._vui_parameters_present_flag);
	bitsUsedSoFar++;

	if (_seqParam[index]._vui_parameters_present_flag)
	{
		//TODO: Implement VUI encoding.
	}//end if _vui_parameters_present_flag...

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_WSPS_RUNOUTOFBITS_WRITE:
	_errorStr = "H264V2:[H264Codec::WriteSeqParamSet] Bits required exceeds max available for picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_WSPS_VLCERROR_WRITE:
	_errorStr = "H264V2:[H264Codec::WriteSeqParamSet] Vlc encoder error";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end WriteSeqParamSet.

/// This method has not been tested and at first glance does not look correct
int H264v2Codec::WriteSeqParamScalingList(IBitStreamWriter* bsw, int* scalingList, int length, int* useDefaultScalingMatrix)
{
	int j, bitCount;
	int bitsUsed = 0;
	int lastScale = 8;
	int nextScale = 8;

	for (j = 0; j < length; j++)
	{
		if (nextScale != 0)
		{
			nextScale = scalingList[j];

			/// deltaScale
			int deltaScale = nextScale - lastScale;
			bitCount = _pHeaderSignedVlcEnc->Encode(deltaScale);  ///< se(v)
			if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
				return(bitsUsed);
			if (bsw)
				bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
			bitsUsed += bitCount;

			if ((j == 0) && (nextScale == 0))
				*useDefaultScalingMatrix = 1;
			else
				*useDefaultScalingMatrix = 0;
		}//end if nextScale...

		lastScale = scalingList[j];

	}//end for j...

	return(bitsUsed);
}//end WriteSeqParamScalingList.

/** Read the sequence parameter set from the bit stream.
This impementation reads sequence parameter set encodings. Each read checks if a
bit underflow will occur after reading. Check for loss of vlc sync wherever possible.
The checks are sufficiently frequent to warrant an early exit GOTO statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@param idx						: Reference to seq param set index read.
@param changedFlag    : Flag to indicate that param set has changed to a new one.
@return								: Run out of bits = 1, more bits available = 0, error = 2.
*/
int H264v2Codec::ReadSeqParamSet(IBitStreamReader* bsr, int remainingBits, int* bitsUsed, int* idx, int* changedFlag)
{
	int bitsUsedSoFar = 0;
	int numBits, i;
	int li;
	int pi;
	int set0_flag;
	int set1_flag;
	int set2_flag;
	int set3_flag;
	int index;

	SeqParamSetH264 tmpParamSet;

	/// Check existance of stream reader.
	if (bsr == NULL)
	{
		_errorStr = "H264V2:[H264Codec::ReadSeqParamSet] No bit stream reader available";
		*bitsUsed = 0;
		return(2);
	}//end if bsr...

	/// The first set of bit fields are fixed with a total of 24 bits.
	if (remainingBits < 24)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// Temporarily read up to the _seq_parameter_set_id that defines the index into the
	/// _seqParam[] array.

	pi = bsr->Read(8);																		///< u(8):_profile_idc
	/// Only baseline profile is supported: _profile_idc = 66.
//	if(pi != 66)
//	{
//		_errorStr = "[H264Codec::ReadSeqParamSet] This implementation only supports the baseline profile";
//		*bitsUsed = 8;
//		return(2);
//	}//end if pi not baseline...
	set0_flag = bsr->Read();															///< u(1):_constraint_set0_flag
	set1_flag = bsr->Read();															///< u(1):_constraint_set1_flag
	set2_flag = bsr->Read();															///< u(1):_constraint_set2_flag
	set3_flag = bsr->Read();															///< u(1):_constraint_set3_flag
	bsr->Read(4);																							///< u(4), reserved_zero_4bits ignored.
	li = bsr->Read(8);																		///< u(8):_level_idc
	bitsUsedSoFar += 24;

	/// _seq_parameter_set_id
	index = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;
	*idx = index;

	/// Range check on the array index.
	if ((index < 0) || (index > 31))
	{
		_errorStr = "[H264Codec::ReadSeqParamSet] Sequence parameter set index out of range";
		*bitsUsed = bitsUsedSoFar;
		return(2);
	}//end if index...

  /// Store the current set.
	tmpParamSet.Copy(&(_seqParam[index]));

	/// The array index is now known and within range so the structure can be checked and loaded from the
	/// temporary variables.
	_seqParam[index]._profile_idc = pi;
	_seqParam[index]._constraint_set0_flag = set0_flag;
	_seqParam[index]._constraint_set1_flag = set1_flag;
	_seqParam[index]._constraint_set2_flag = set2_flag;
	_seqParam[index]._constraint_set3_flag = set3_flag;
	_seqParam[index]._level_idc = li;
	_seqParam[index]._seq_parameter_set_id = index;

	if ((_seqParam[index]._profile_idc == 100) || (_seqParam[index]._profile_idc == 110) ||
		(_seqParam[index]._profile_idc == 122) || (_seqParam[index]._profile_idc == 144))
	{
		/// _chroma_format_idc
		_seqParam[index]._chroma_format_idc = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		if (_seqParam[index]._chroma_format_idc == 3)
		{
			_seqParam[index]._residual_colour_transform_flag = bsr->Read(); ///< u(1)
			bitsUsedSoFar++;
		}//end if _chroma_format_idc...

		  /// _bit_depth_luma_minus8
		_seqParam[index]._bit_depth_luma_minus8 = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		/// _bit_depth_chroma_minus8
		_seqParam[index]._bit_depth_chroma_minus8 = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		/// Two flags.
		_seqParam[index]._qpprime_y_zero_transform_bypass_flag = bsr->Read(); ///< u(1)
		_seqParam[index]._seq_scaling_matrix_present_flag = bsr->Read(); ///< u(1)
		bitsUsedSoFar += 2;

		/// _seq_scaling_list_present_flag[]
		if (_seqParam[index]._seq_scaling_matrix_present_flag)
		{
			for (i = 0; i < 8; i++)
			{
				_seqParam[index]._seq_scaling_list_present_flag[i] = bsr->Read(); ///< u(1)
				bitsUsedSoFar++;

				if (_seqParam[index]._seq_scaling_list_present_flag[i])
				{
					if (i < 6)
						bitsUsedSoFar += ReadSeqParamScalingList(bsr, _seqParam[index]._scaling_list_4x4[i], 16, &(_seqParam[index]._use_default_scaling_matrix_4x4_flag[i]));
					else
						bitsUsedSoFar += ReadSeqParamScalingList(bsr, _seqParam[index]._scaling_list_8x8[i - 6], 64, &(_seqParam[index]._use_default_scaling_matrix_8x8_flag[i - 6]));
				}//end if _seq_scaling_list_present_flag...

				if (bitsUsedSoFar > remainingBits)
					goto H264V2_RSPS_RUNOUTOFBITS_READ;
			}//end for i...
		}//end if _seq_scaling_matrix_present_flag...

	}//end if _profile_idc...

	  /// _log2_max_frame_num_minus4
	_seqParam[index]._log2_max_frame_num_minus4 = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// _pic_order_cnt_type
	_seqParam[index]._pic_order_cnt_type = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	if (_seqParam[index]._pic_order_cnt_type == 0)
	{
		/// _log2_max_pic_order_cnt_lsb_minus4
		_seqParam[index]._log2_max_pic_order_cnt_lsb_minus4 = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;
	}//end if _pic_order_cnt_type...
	else if (_seqParam[index]._pic_order_cnt_type == 1)
	{
		/// _delta_pic_order_always_zero_flag
		_seqParam[index]._delta_pic_order_always_zero_flag = bsr->Read();
		bitsUsedSoFar++;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		/// _offset_for_non_ref_pic
		_seqParam[index]._offset_for_non_ref_pic = _pHeaderSignedVlcDec->Decode(bsr);
		numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		/// _offset_for_top_to_bottom_field
		_seqParam[index]._offset_for_top_to_bottom_field = _pHeaderSignedVlcDec->Decode(bsr);
		numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		/// _num_ref_frames_in_pic_order_cnt_cycle
		_seqParam[index]._num_ref_frames_in_pic_order_cnt_cycle = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;

		for (i = 0; i < _seqParam[index]._num_ref_frames_in_pic_order_cnt_cycle; i++)
		{
			/// _offset_for_ref_frame[256]
			_seqParam[index]._offset_for_ref_frame[i] = _pHeaderSignedVlcDec->Decode(bsr);
			numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_RSPS_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSPS_RUNOUTOFBITS_READ;
		}//end for i...

	}//end else if _pic_order_cnt_type...

	/// _num_ref_frames
	_seqParam[index]._num_ref_frames = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// _gaps_in_frame_num_value_allowed_flag
	_seqParam[index]._gaps_in_frame_num_value_allowed_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// _pic_width_in_mbs_minus1
	_seqParam[index]._pic_width_in_mbs_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// _pic_height_in_map_units_minus1
	_seqParam[index]._pic_height_in_map_units_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// _frame_mbs_only_flag
	_seqParam[index]._frame_mbs_only_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	if (!_seqParam[index]._frame_mbs_only_flag)
	{
		/// _mb_adaptive_frame_field_flag
		_seqParam[index]._mb_adaptive_frame_field_flag = bsr->Read();
		bitsUsedSoFar++;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;
	}//end if !_frame_mbs_only_flag...

	/// _direct_8x8_inference_flag
	_seqParam[index]._direct_8x8_inference_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	/// _frame_cropping_flag
	_seqParam[index]._frame_cropping_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	if (_seqParam[index]._frame_cropping_flag)
	{
		/// _frame_crop_left_offset
		_seqParam[index]._frame_crop_left_offset = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;
		/// _frame_crop_right_offset
		_seqParam[index]._frame_crop_right_offset = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;
		/// _frame_crop_top_offset
		_seqParam[index]._frame_crop_top_offset = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;
		/// _frame_crop_bottom_offset
		_seqParam[index]._frame_crop_bottom_offset = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSPS_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSPS_RUNOUTOFBITS_READ;
	}//end if _frame_cropping_flag...

	/// _vui_parameters_present_flag
	_seqParam[index]._vui_parameters_present_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSPS_RUNOUTOFBITS_READ;

	if (_seqParam[index]._vui_parameters_present_flag)
	{
		//TODO: ReadSeqParamVuiParams() does not work properly and must be debugged.
			//numBits = ReadSeqParamVuiParams(bsr);
			//bitsUsedSoFar += numBits;
			//if( bitsUsedSoFar > remainingBits )
			//	goto H264V2_RSPS_RUNOUTOFBITS_READ;

		/// Dummy read all the bits except the last one to leave the bsr pointer on the last bit.
		int pos = bsr->GetStreamBitPos();
		numBits = remainingBits - bitsUsedSoFar - 1;
		bsr->Read(numBits);
		/// Search backwards until the trailing stop bit is found.
		pos = bsr->GetStreamBitPos();
		while (bsr->Peek(pos, 1) != 1)
		{
			pos++;
			numBits--;
		}//end while Peek...
		/// Set the bsr to point at the stop bit.
		bsr->Seek(pos);
		bitsUsedSoFar += numBits;

	}//end if _vui_parameters_present_flag...

  /// Check if this new set is different to the old stored at this index.
	*changedFlag = 1;
	if (tmpParamSet.Equals(&(_seqParam[index])))
		*changedFlag = 0;

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_RSPS_RUNOUTOFBITS_READ:
	_errorStr = "[H264Codec::ReadSeqParamSet] Insufficient bits to decode the picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_RSPS_NOVLC_READ:
	_errorStr = "[H264Codec::ReadSeqParamSet] No valid vlc in bit stream";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end ReadSeqParamSet.

/// This method has not been tested.
int H264v2Codec::ReadSeqParamScalingList(IBitStreamReader* bsr, int* scalingList, int length, int* useDefaultScalingMatrix)
{
	int j, numBits;
	int bitsUsed = 0;
	int lastScale = 8;
	int nextScale = 8;

	for (j = 0; j < length; j++)
	{
		if (nextScale != 0)
		{
			/// deltaScale 
			int deltaScale = _pHeaderSignedVlcDec->Decode(bsr); ///< se(v)
			numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				return(bitsUsed);
			bitsUsed += numBits;

			nextScale = (lastScale + deltaScale + 256) % 256;

			if ((j == 0) && (nextScale == 0))
				*useDefaultScalingMatrix = 1;
			else
				*useDefaultScalingMatrix = 0;
		}//end if nextScale...

		if (nextScale == 0)
			scalingList[j] = lastScale;
		else
			scalingList[j] = nextScale;
		lastScale = scalingList[j];

	}//end for j...

	return(bitsUsed);
}//end ReadSeqParamScalingList.

/** Read the vui parameters from a sequence parameter encoded bit stream.
This is currently a dummy read without any checking.
@param bsr  : Bit stream reader to read from.
@return     : Number of bits used.
*/
int H264v2Codec::ReadSeqParamVuiParams(IBitStreamReader* bsr)
{
	int bitsUsed = 0;

	/// Aspect ratio info
	unsigned int aspect_ratio_info_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (aspect_ratio_info_present_flag)
	{
		unsigned int aspect_ratio_idc = bsr->Read(8); ///< u(8)
		bitsUsed += 8;
		if (aspect_ratio_idc == 255) ///< Extended_SAR
		{
			unsigned int sar_width = bsr->Read(16); ///< u(16)
			bitsUsed += 16;
			unsigned int sar_height = bsr->Read(16); ///< u(16)
			bitsUsed += 16;
		}//end if aspect_ratio_idc...
	}//end if aspect_ratio_info_present_flag...

	/// Overscan info
	unsigned int overscan_info_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (overscan_info_present_flag)
	{
		unsigned int overscan_appropriate_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
	}//end if overscan_info_present_flag...

	/// Video signal type info
	unsigned int video_signal_type_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (video_signal_type_present_flag)
	{
		unsigned int video_format = bsr->Read(3); ///< u(3)
		bitsUsed += 3;
		unsigned int video_full_range_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
		unsigned int colour_description_present_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
		if (colour_description_present_flag)
		{
			unsigned int colour_primaries = bsr->Read(8); ///< u(8)
			bitsUsed += 8;
			unsigned int transfer_characteristics = bsr->Read(8); ///< u(8)
			bitsUsed += 8;
			unsigned int matrix_coefficients = bsr->Read(8); ///< u(8)
			bitsUsed += 8;
		}//end if colour_description_present_flag...
	}//end if video_signal_type_present_flag...

	/// Chroma location info
	unsigned int chroma_loc_info_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (chroma_loc_info_present_flag)
	{
		unsigned int chroma_sample_loc_type_top_field = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int chroma_sample_loc_type_bottom_field = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	}//end if chroma_loc_info_present_flag...

	/// Timing info
	unsigned int timing_info_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (timing_info_present_flag)
	{
		unsigned int num_units_in_tick = bsr->Read(32); ///< u(32)
		bitsUsed += 32;
		unsigned int time_scale = bsr->Read(32); ///< u(32)
		bitsUsed += 32;
		unsigned int fixed_frame_rate_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
	}//end if timing_info_present_flag...

	/// HRD parameter info
	unsigned int nal_hrd_parameters_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (nal_hrd_parameters_present_flag)
	{
		bitsUsed += ReadSeqParamVuiHrdParams(bsr);
	}//end if nal_hrd_parameters_present_flag...
	unsigned int vcl_hrd_parameters_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (vcl_hrd_parameters_present_flag)
	{
		bitsUsed += ReadSeqParamVuiHrdParams(bsr);
	}//end if vcl_hrd_parameters_present_flag...
	if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag)
	{
		unsigned int low_delay_hrd_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
	}//end if nal_hrd_parameters_present_flag...

	/// Picture structure info
	unsigned int pic_struct_present_flag = bsr->Read(); ///< u(1)
	bitsUsed++;

	/// Bit stream restriction info
	unsigned int bitstream_restriction_flag = bsr->Read(); ///< u(1)
	bitsUsed++;
	if (bitstream_restriction_flag)
	{
		unsigned int motion_vectors_over_pic_boundaries_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
		unsigned int max_bytes_per_pic_demon = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int max_bits_per_mb_demon = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int log2_max_mv_length_horizontal = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int log2_max_mv_length_vertical = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int num_reorder_frames = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int max_dec_frame_buffering = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	}//end if bitstream_restriction_flag...

	return(bitsUsed);
}//end ReadSeqParamVuiParams.

/** Read the hrd parameters from a vui sequence parameter encoded bit stream.
This is currently a dummy read without any checking.
@param bsr  : Bit stream reader to read from.
@return     : Number of bits used.
*/
int H264v2Codec::ReadSeqParamVuiHrdParams(IBitStreamReader* bsr)
{
	int bitsUsed = 0;

	unsigned int cpb_cnt_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
	bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();

	unsigned int bit_rate_scale = bsr->Read(4); ///< u(4)
	bitsUsed += 4;

	unsigned int cpb_size_scale = bsr->Read(4); ///< u(4)
	bitsUsed += 4;

	for (unsigned int schedSelIdx = 0; schedSelIdx <= cpb_cnt_minus1; schedSelIdx++)
	{
		/// This is a dummy method but the actual method will fill bit_rate_value_minus1[schedSelIdx] array.
		unsigned int bit_rate_value_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int cpb_size_value_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr); ///< ue(v)
		bitsUsed += _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		unsigned int cbr_flag = bsr->Read(); ///< u(1)
		bitsUsed++;
	}//end for schedSelIdx...

	unsigned int initial_cpb_removal_delay_length_minus1 = bsr->Read(5); ///< u(5)
	bitsUsed += 5;
	unsigned int cpb_removal_delay_length_minus1 = bsr->Read(5); ///< u(5)
	bitsUsed += 5;
	unsigned int dpb_output_delay_length_minus1 = bsr->Read(5); ///< u(5)
	bitsUsed += 5;
	unsigned int time_offset_length = bsr->Read(5); ///< u(5)
	bitsUsed += 5;

	return(bitsUsed);
}//endReadSeqParamVuiHrdParams. 

/** Set the Picture parameter set.
Set one of the pic param sets defined by the index parameters where the
values are determined by interpreting the codec parameters.
@param index	:	Pic parameter set to modify [0..255].
@param seqRef	: Seq param set to reference.
@return				: Success = 1, failure = 0.
*/
int H264v2Codec::SetPicParamSet(int index, int seqRef)
{
	if ((index > 255) || (seqRef > 31))
		return(0);

	_picParam[index]._pic_parameter_set_id = index;	///< The index for this definition of picture params [0..255].
	_picParam[index]._seq_parameter_set_id = seqRef;	///< This pic param set refers to this seq param set. Range = [0..31].
	_picParam[index]._entropy_coding_mode_flag = 0;			///< Baseline profile = 0. CAVLC/CABAC = 0/1.
	_picParam[index]._pic_order_present_flag = 0;			///< Pic order count syntax elements are present in the slice header.
	_picParam[index]._num_slice_groups_minus1 = 0;			///< = 0 (baseline profile). Num of slice groups for a pic.
	_picParam[index]._slice_group_map_type = 0;			///< Defines map units to slice groups. Range = [0..6].
	/// Not initialised: _run_length_minus1[7];									///< Slice group mapping.
	/// Not initialised: _top_left[7];													///< Slice group mapping.
	/// Not initialised: _bottom_right[7];											///< Slice group mapping.
	_picParam[index]._slice_group_change_direction_flag = 0;			///< Slice group mapping.
	_picParam[index]._slice_group_change_rate_minus1 = 0;			///< Slice group mapping.
	/// Not initialised: _slice_group_id[7];										///< Slice group mapping.
	_picParam[index]._num_ref_idx_l0_active_minus1 = 0;			///< Max ref index for ref pic list0.
	_picParam[index]._num_ref_idx_l1_active_minus1 = 0;			///< Max ref index for ref pic list1. (For B-Slices)
	_picParam[index]._weighted_pred_flag = 0;			///< Indicates weighted prediction applied to P-Slices.
	_picParam[index]._weighted_bipred_idc = 0;			///< Weighted prediction type applied to B-Slices.
	_picParam[index]._pic_init_qp_minus26 = 0;			///< Initial value of lum SliceQP - 26 for each slice. The slice_qp_delta modifies this value. Rng = [-26..25].
	_picParam[index]._pic_init_qs_minus26 = 0;			///< For SP and SI slices.
	_picParam[index]._chroma_qp_index_offset = 0;			///< Offset added to lum QP (and QS) for Cb chr QP values. Rng = [-12..12].
	_picParam[index]._second_chroma_qp_index_offset = 0;			///< For Cr chr QP. When not present = _chroma_qp_index_offset above.
	_picParam[index]._deblocking_filter_control_present_flag = 0;			///< Indicates presence of elements in slice header to change the characteristics of the deblocking filter.
	_picParam[index]._constrained_intra_pred_flag = 1;			///< = 1. Indicates that intra macroblock prediction can only be done from other intra macroblocks. 
	_picParam[index]._redundant_pic_cnt_present_flag = 0;			///< Indicates that redundant pic count elements are in the slice header.
	_picParam[index]._transform_8x8_mode_flag = 0;			///< = 0. Indicates 8x8 transform is used. When not present = 0.
	_picParam[index]._pic_scaling_matrix_present_flag = 0;			///< = 0. Indicates params are present to modify the scaling lists specified in the seq param sets.
	/// Not initialised:_pic_scaling_list_present_flag[8];			///< Scaling list syntax structure present for scaling list i = 0..7.

	_picParam[index]._pic_size_in_map_units_minus1 = ((_width / 16)*(_height / 16)) - 1;	///< Num of map units (macroblocks for frames) in the pic. Macroblock width * height.

	return(1);
}//end SetPicParamSet.

/** Write the picture parameter set to a bit stream.
The pic param set must be correctly defined before this method is called. The
vlc encoding is performed first before writing. Each write checks if a bit
overflow will occur before writing. The check is sufficiently frequent to
warrant an early exit GOTO statement. If the input stream param is NULL then
this method is used to count the bits only.
@param bsw					: Stream to write into.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@param index				: Picture parameter set index to write.
@return							: Run out of bits = 1, more bits available = 0, Vlc error = 2, unsupported parameter set = 3.
*/
int H264v2Codec::WritePicParamSet(IBitStreamWriter* bsw, int allowedBits, int* bitsUsed, int index)
{
	int	bitCount;
	int bitsUsedSoFar = 0;

	/// _pic_parameter_set_id
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_picParam[index]._pic_parameter_set_id);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _seq_parameter_set_id
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_picParam[index]._seq_parameter_set_id);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _entropy_coding_mode_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_picParam[index]._entropy_coding_mode_flag);
	bitsUsedSoFar++;

	/// _pic_order_present_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_picParam[index]._pic_order_present_flag);
	bitsUsedSoFar++;

	/// _num_slice_groups_minus1
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_picParam[index]._num_slice_groups_minus1);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	if (_picParam[index]._num_slice_groups_minus1 > 0)
	{
		// TODO: Add support for slice groups here
		goto H264V2_WPPS_MODEERROR_WRITE;

		/// _slice_group_map_type, _run_length_minus1[7], _top_left[7], _bottom_right[7], _slice_group_change_direction_flag,
		/// _slice_group_change_rate_minus1, _pic_size_in_map_units_minus1, _slice_group_id[].
	}//end if _num_slice_groups_minus1...

	/// _num_ref_idx_l0_active_minus1
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_picParam[index]._num_ref_idx_l0_active_minus1);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _num_ref_idx_l1_active_minus1
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_picParam[index]._num_ref_idx_l1_active_minus1);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _weighted_pred_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_picParam[index]._weighted_pred_flag);
	bitsUsedSoFar++;

	/// _weighted_bipred_idc
	if ((bitsUsedSoFar + 2) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(2, _picParam[index]._weighted_bipred_idc);
	bitsUsedSoFar += 2;

	/// _pic_init_qp_minus26
	bitCount = _pHeaderSignedVlcEnc->Encode(_picParam[index]._pic_init_qp_minus26);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _pic_init_qs_minus26
	bitCount = _pHeaderSignedVlcEnc->Encode(_picParam[index]._pic_init_qs_minus26);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _chroma_qp_index_offset
	bitCount = _pHeaderSignedVlcEnc->Encode(_picParam[index]._chroma_qp_index_offset);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_WPPS_VLCERROR_WRITE;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// _deblocking_filter_control_present_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_picParam[index]._deblocking_filter_control_present_flag);
	bitsUsedSoFar++;

	/// _constrained_intra_pred_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_picParam[index]._constrained_intra_pred_flag);
	bitsUsedSoFar++;

	/// _redundant_pic_cnt_present_flag
	if ((bitsUsedSoFar + 1) > allowedBits)
		goto H264V2_WPPS_RUNOUTOFBITS_WRITE;
	if (bsw)
		bsw->Write(_picParam[index]._redundant_pic_cnt_present_flag);
	bitsUsedSoFar++;

	//TODO: Implement if more_rbsp_data() section here.
	/// _transform_8x8_mode_flag, _pic_scaling_matrix_present_flag, _pic_scaling_list_present_flag[8],
	/// _second_chroma_qp_index_offset.

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_WPPS_RUNOUTOFBITS_WRITE:
	_errorStr = "H264V2:[H264Codec::WritePicParamSet] Bits required exceeds max available for picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_WPPS_VLCERROR_WRITE:
	_errorStr = "H264V2:[H264Codec::WritePicParamSet] Vlc encoder error";
	*bitsUsed = bitsUsedSoFar;
	return(2);

H264V2_WPPS_MODEERROR_WRITE:
	_errorStr = "H264V2:[H264Codec::WritePicParamSet] Picture parameter not implemented";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end WritePicParamSet.

/** Read the picture parameter set from the bit stream.
This impementation reads picture parameter set encodings. Each read checks if a
bit underflow will occur after reading. Check for loss of vlc sync wherever possible.
The checks are sufficiently frequent to warrant an early exit GOTO statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@param idx						: Reference to pic param set index read.
@param changedFlag    : Flag to indicate that param set has changed to a new one.
@return								: Run out of bits = 1, more bits available = 0, error = 2.
*/
int H264v2Codec::ReadPicParamSet(IBitStreamReader* bsr, int remainingBits, int* bitsUsed, int* idx, int* changedFlag)
{
	int bitsUsedSoFar = 0;
	int numBits;
	PicParamSetH264 tmpParamSet;

	/// Check existance of stream reader.
	if (bsr == NULL)
	{
		_errorStr = "[H264Codec::ReadPicParamSet] No bit stream reader available";
		*bitsUsed = 0;
		return(2);
	}//end if bsr...

	/// _pic_parameter_set_id
	int index = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;
	*idx = index;

	/// Range check on the pic param array index.
	if ((index < 0) || (index > 255))
	{
		_errorStr = "[H264Codec::ReadPicParamSet] Picture parameter set index out of range";
		*bitsUsed = bitsUsedSoFar;
		return(2);
	}//end if index...

  /// Store the current set.
	tmpParamSet.Copy(&(_picParam[index]));

	/// The decoded index identifies the param set to use.
	_picParam[index]._pic_parameter_set_id = index;

	/// _seq_parameter_set_id
	_picParam[index]._seq_parameter_set_id = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// Range check on the seq param array index. This does not test if the data in the _seqParam[]
	/// array for this id is valid.
	if ((_picParam[index]._seq_parameter_set_id < 0) || (_picParam[index]._seq_parameter_set_id > 31))
	{
		_errorStr = "H264V2:[H264Codec::ReadPicParamSet] Sequence parameter set index of picture parameter set is out of range";
		*bitsUsed = bitsUsedSoFar;
		return(2);
	}//end if index...

	/// _entropy_coding_mode_flag
	_picParam[index]._entropy_coding_mode_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _pic_order_present_flag
	_picParam[index]._pic_order_present_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _num_slice_groups_minus1
	_picParam[index]._num_slice_groups_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	if (_picParam[index]._num_slice_groups_minus1 > 0)
	{
		// TODO: Add support for slice groups here
		goto H264V2_RPPS_NOMODE_READ;

		/// _slice_group_map_type, _run_length_minus1[7], _top_left[7], _bottom_right[7], _slice_group_change_direction_flag,
		/// _slice_group_change_rate_minus1, _pic_size_in_map_units_minus1, _slice_group_id[].
	}//end if _num_slice_groups_minus1...

	/// _num_ref_idx_l0_active_minus1
	_picParam[index]._num_ref_idx_l0_active_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _num_ref_idx_l1_active_minus1
	_picParam[index]._num_ref_idx_l1_active_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _weighted_pred_flag
	_picParam[index]._weighted_pred_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _weighted_bipred_idc
	_picParam[index]._weighted_bipred_idc = bsr->Read(2);
	bitsUsedSoFar += 2;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _pic_init_qp_minus26
	_picParam[index]._pic_init_qp_minus26 = _pHeaderSignedVlcDec->Decode(bsr);
	numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _pic_init_qs_minus26
	_picParam[index]._pic_init_qs_minus26 = _pHeaderSignedVlcDec->Decode(bsr);
	numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _chroma_qp_index_offset
	_picParam[index]._chroma_qp_index_offset = _pHeaderSignedVlcDec->Decode(bsr);
	numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RPPS_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _deblocking_filter_control_present_flag
	_picParam[index]._deblocking_filter_control_present_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _constrained_intra_pred_flag
	_picParam[index]._constrained_intra_pred_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	/// _redundant_pic_cnt_present_flag
	_picParam[index]._redundant_pic_cnt_present_flag = bsr->Read();
	bitsUsedSoFar++;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RPPS_RUNOUTOFBITS_READ;

	//TODO: Implement if more_rbsp_data() section here.
	/// _transform_8x8_mode_flag, _pic_scaling_matrix_present_flag, _pic_scaling_list_present_flag[8],
	/// _second_chroma_qp_index_offset.

  /// Check if this new set is different to the old stored at this index.
	*changedFlag = 1;
	if (tmpParamSet.Equals(&(_picParam[index])))
		*changedFlag = 0;

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_RPPS_RUNOUTOFBITS_READ:
	_errorStr = "[H264Codec::ReadPicParamSet] Insufficient bits to decode the picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_RPPS_NOVLC_READ:
	_errorStr = "[H264Codec::ReadPicParamSet] No valid vlc in bit stream";
	*bitsUsed = bitsUsedSoFar;
	return(2);

H264V2_RPPS_NOMODE_READ:
	_errorStr = "[H264Codec::ReadPicParamSet] Picture parameter not implemented";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end ReadPicParamSet.

/** Get the codec parameters.
Load the codec params from the picture param set indexed by
the input param. The sequence param set is implied in the referenced
pic param set. The pic/seq param set must be previously defined
prior to calling this method.
@param picParamSet	:	Pic parameter set to get from [0..255].
@return							: Success = 1, failure = 0.
*/
int H264v2Codec::GetCodecParams(int picParamSet)
{
	///----------- Some parameter set range checking ------------------------------
	if ((picParamSet < 0) || (picParamSet > 255))
	{
		_errorStr = "[H264Codec::GetCodecParams] Picture parameter set out of range";
		return(0);
	}//end if picParamSet...
	if (_picParam[picParamSet]._pic_parameter_set_id != picParamSet)
	{
		_errorStr = "[H264Codec::GetCodecParams] Picture parameter set mismatch";
		return(0);
	}//end if _pic_parameter_set_id...
	int seqParamSet = _picParam[picParamSet]._seq_parameter_set_id;
	if ((seqParamSet < 0) || (seqParamSet > 31))
	{
		_errorStr = "[H264Codec::GetCodecParams] Sequence parameter set out of range";
		return(0);
	}//end if seqParamSet...
	if (_seqParam[seqParamSet]._seq_parameter_set_id != seqParamSet)
	{
		_errorStr = "[H264Codec::GetCodecParams] Sequence parameter set mismatch";
		return(0);
	}//end if _seq_parameter_set_id...
	/// Modify the codec seq param set to that associated with this pic param set.
	_currSeqParam = seqParamSet;

	///----------- Extract params of interest for codec -----------------------
	/// Get width and height in pels.
	_width = (_seqParam[seqParamSet]._pic_width_in_mbs_minus1 + 1) * 16;
	_height = (_seqParam[seqParamSet]._pic_height_in_map_units_minus1 + 1) * 16;
	_seqParamSetLog2MaxFrameNumMinus4 = _seqParam[seqParamSet]._log2_max_frame_num_minus4;

	///----------- Check params for this implementation -----------------------

	/// Sequence parameter implementation.
	if ((_seqParam[seqParamSet]._profile_idc != 66) ||
		(_seqParam[seqParamSet]._constraint_set0_flag != 0) ||
		(_seqParam[seqParamSet]._constraint_set1_flag != 0) ||
		(_seqParam[seqParamSet]._constraint_set2_flag != 0) ||
		(_seqParam[seqParamSet]._constraint_set3_flag != 0) ||
		(_seqParam[seqParamSet]._level_idc != 20) ||
		(_seqParam[seqParamSet]._chroma_format_idc != 1) ||
		(_seqParam[seqParamSet]._bit_depth_luma_minus8 != 0) ||
		(_seqParam[seqParamSet]._bit_depth_chroma_minus8 != 0) ||
		(_seqParam[seqParamSet]._qpprime_y_zero_transform_bypass_flag != 0) ||
		(_seqParam[seqParamSet]._seq_scaling_matrix_present_flag != 0) ||
		(_seqParam[seqParamSet]._pic_order_cnt_type != 2) ||
		(_seqParam[seqParamSet]._num_ref_frames != 1) ||
		(_seqParam[seqParamSet]._gaps_in_frame_num_value_allowed_flag != 0) ||
		(_seqParam[seqParamSet]._frame_mbs_only_flag != 1) ||
		(_seqParam[seqParamSet]._mb_adaptive_frame_field_flag != 0) ||
		(_seqParam[seqParamSet]._direct_8x8_inference_flag != 0) ||
		(_seqParam[seqParamSet]._frame_cropping_flag != 0) ||
		(_seqParam[seqParamSet]._vui_parameters_present_flag != 0))
	{
		_errorStr = "[H264Codec::GetCodecParams] Sequence parameters not implemented";
		return(0);
	}//end if _profile_idc...

	/// Picture parameter implementation.
	if ((_picParam[picParamSet]._entropy_coding_mode_flag != 0) ||
		(_picParam[picParamSet]._pic_order_present_flag != 0) ||
		(_picParam[picParamSet]._num_slice_groups_minus1 != 0) ||
		(_picParam[picParamSet]._num_ref_idx_l0_active_minus1 != 0) ||
		(_picParam[picParamSet]._num_ref_idx_l1_active_minus1 != 0) ||
		(_picParam[picParamSet]._weighted_pred_flag != 0) ||
		(_picParam[picParamSet]._weighted_bipred_idc != 0) ||
		(_picParam[picParamSet]._constrained_intra_pred_flag != 1) ||
		(_picParam[picParamSet]._redundant_pic_cnt_present_flag != 0) ||
		(_picParam[picParamSet]._transform_8x8_mode_flag != 0) ||
		(_picParam[picParamSet]._pic_scaling_matrix_present_flag != 0))
	{
		_errorStr = "[H264Codec::GetCodecParams] Picture parameters not implemented";
		return(0);
	}//end if _profile_idc...

	return(1);
}//end GetCodecParams.

/** Write the NAL header to a bit stream.
The NAL header data must be correctly defined before this method is called. The
vlc encoding is performed first before writing. Each write checks if a bit overflow
will occur before writing. The check is sufficiently frequent to warrant an early
exit GOTO statement. If the input stream param is NULL then this method is used
to count the bits only.
@param bsw					: Stream to write into.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@return							: Run out of bits = 1, more bits available = 0, Vlc error = 2.
*/
int H264v2Codec::WriteNALHeader(IBitStreamWriter* bsw, int allowedBits, int* bitsUsed)
{
	/// The NAL header is a fixed 8 bit sequence.
	if (allowedBits >= 8)
	{
		if (bsw)
		{
			bsw->Write(1, 0);												///< Forbidden zero bit f(1).
			bsw->Write(2, (_nal._ref_idc & 3));			///< u(2).		
			bsw->Write(5, (_nal._unit_type & 31));	///< u(5).		
		}//end if bsw != NULL...
	}//end if allowedBits...
	else
	{
		_errorStr = "H264V2:[WriteNALHeader] Bits required exceeds max available for NAL header";
		*bitsUsed = 0;
		return(1);
	}///end else...

	*bitsUsed = 8;
	return(0);
}//end WriteNALHeader.

/** Read the NAL header from the bit stream.
This impementation reads the NAL header encoding. Each read checks if a
bit underflow will occur after reading. Check for loss of vlc sync
wherever possible. The checks are sufficiently frequent to warrant
an early exit GOTO statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@return								: Run out of bits = 1, more bits available = 0, error = 2.
*/
int H264v2Codec::ReadNALHeader(IBitStreamReader* bsr, int remainingBits, int* bitsUsed)
{
	/// The NAL header is a fixed 8 bit sequence.
	if (remainingBits >= 8)
	{
		bsr->Read();													///< Discard forbidden zero bit f(1).
		_nal._ref_idc = 3 & bsr->Read(2);		///< u(2).
		_nal._unit_type = 31 & bsr->Read(5);	///< u(5).
	}//end if remainingBits...
	else
	{
		_errorStr = "H264V2:[ReadNALHeader] Insufficient bits to decode the NAl header";
		*bitsUsed = 0;
		return(1);
	}//end else...

	*bitsUsed = 8;
	return(0);
}//end ReadNALHeader.

/** Write the slice layer header to a bit stream.
The encodings of all the slice data must be correctly defined before
this method is called. The vlc encoding is performed first before
writing. Each write checks if a bit overflow will occur before writing.
The check is sufficiently frequent to warrant an early exit GOTO
statement. If the input stream param is NULL then this method is used
to count the bits only.
@param bsw					: Stream to write into.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@return							: Run out of bits = 1, more bits available = 0, Vlc error = 2.
*/
int H264v2Codec::WriteSliceLayerHeader(IBitStreamWriter* bsw, int allowedBits, int* bitsUsed)
{
	int	  bitCount;
	int   bitsUsedSoFar = 0;
	char  errInfo[64];
	char  buff[64];

		/// Limited implementation of modes and types for Baseline profile.
	if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::I_Slice_All) &&
		(_slice._type != SliceHeaderH264::P_Slice) && (_slice._type != SliceHeaderH264::P_Slice_All))
		goto H264V2_WSLH_MODEERROR_WRITE;

	///-------------------------- Addr of first macroblock -----------------------------------
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._first_mb_in_slice);
	if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
	{
		if ((bitsUsedSoFar + bitCount) <= allowedBits)
		{
			if (bsw)
				bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	}//end if bitCount...
	else
	{
		strcpy(errInfo, "_first_mb_in_slice=");
		sprintf((char *)buff, "%d", _slice._first_mb_in_slice);
		strcat(errInfo, buff);
		// strcat(errInfo, itoa(_slice._first_mb_in_slice, buff, 10));
		goto H264V2_WSLH_VLCERROR_WRITE;
	}//end else...
	bitsUsedSoFar += bitCount;

	///-------------------------- Slice Type -------------------------------------------------
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._type);
	if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
	{
		if ((bitsUsedSoFar + bitCount) <= allowedBits)
		{
			if (bsw)
				bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	}//end if bitCount...
	else
	{
		strcpy(errInfo, "_type=");
		sprintf((char *)buff, "%d", _slice._type);
		strcat(errInfo, buff);
		//strcat(errInfo, itoa(_slice._type, buff, 10));
		goto H264V2_WSLH_VLCERROR_WRITE;
	}//end else...
	bitsUsedSoFar += bitCount;

	///-------------------------- Pic Param Set Id --------------------------------------------
	bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._pic_parameter_set_id);
	if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
	{
		if ((bitsUsedSoFar + bitCount) <= allowedBits)
		{
			if (bsw)
				bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	}//end if bitCount...
	else
	{
		strcpy(errInfo, "_pic_parameter_set_id=");
		sprintf((char *)buff, "%d", _slice._pic_parameter_set_id);
		strcat(errInfo, buff);
		// strcat(errInfo, itoa(_slice._pic_parameter_set_id, buff, 10));
		goto H264V2_WSLH_VLCERROR_WRITE;
	}//end else...
	bitsUsedSoFar += bitCount;

	///-------------------------- Frame Number -----------------------------------------------
	bitCount = _seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._log2_max_frame_num_minus4 + 4;
	if ((bitsUsedSoFar + bitCount) <= allowedBits)
	{
		if (bsw)
			bsw->Write(bitCount, _slice._frame_num);
	}//end if bitsUsedSoFar...
	else
		goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	bitsUsedSoFar += bitCount;

	///-------------------------- Field Flags ----------------------------------------------
	if (!_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._frame_mbs_only_flag)
	{
		if ((bitsUsedSoFar + 2) <= allowedBits)	///< Both flags together.
		{
			if (bsw)
				bsw->Write(_slice._field_pic_flag);
			bitsUsedSoFar++;
			if (_slice._field_pic_flag)
			{
				if (bsw)
					bsw->Write(_slice._bottom_field_flag);
				bitsUsedSoFar++;
			}//end if _field_pic_flag...
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	}//end if !_frame_mbs_only_flag...

	///-------------------------- IDR Frame Count ------------------------------------------
	if (_nal._unit_type == NalHeaderH264::IDR_Slice)
	{
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._idr_pic_id);
		if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
		{
			if ((bitsUsedSoFar + bitCount) <= allowedBits)
			{
				if (bsw)
				{
					if (bitCount <= 32)
						bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
					else
						bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetExtCode(), _pHeaderUnsignedVlcEnc->GetCode());
				}//end if bsw...
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if bitCount...
		else
		{
			strcpy(errInfo, "_idr_pic_id=");
			sprintf((char *)buff, "%d", _slice._idr_pic_id);
			strcat(errInfo, buff);
			//strcat(errInfo, itoa(_slice._idr_pic_id, buff, 10));
			goto H264V2_WSLH_VLCERROR_WRITE;
		}//end else...
		bitsUsedSoFar += bitCount;
	}//end if _unit_type...

	///-------------------------- Picture Order Count ---------------------------------------
	if (_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._pic_order_cnt_type == 0)
	{
		bitCount = _seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._log2_max_pic_order_cnt_lsb_minus4 + 4;
		if ((bitsUsedSoFar + bitCount) <= allowedBits)
		{
			if (bsw)
				bsw->Write(bitCount, _slice._pic_order_cnt_lsb);
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		bitsUsedSoFar += bitCount;

		if (_picParam[_slice._pic_parameter_set_id]._pic_order_present_flag && !_slice._field_pic_flag)
		{
			bitCount = _pHeaderSignedVlcEnc->Encode(_slice._delta_pic_order_cnt_bottom);
			if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
			{
				if ((bitsUsedSoFar + bitCount) <= allowedBits)
				{
					if (bsw)
						bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
				}//end if bitsUsedSoFar...
				else
					goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
			}//end if bitCount...
			else
			{
				strcpy(errInfo, "_delta_pic_order_cnt_bottom=");
				sprintf((char *)buff, "%d", _slice._delta_pic_order_cnt_bottom);
				strcat(errInfo, buff);
				// strcat(errInfo, itoa(_slice._delta_pic_order_cnt_bottom, buff, 10));
				goto H264V2_WSLH_VLCERROR_WRITE;
			}//end else...
			bitsUsedSoFar += bitCount;
		}//end if _pic_order_present_flag...
	}//end if _pic_order_cnt_type...
	else if ((_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._pic_order_cnt_type == 1) &&
		!_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._delta_pic_order_always_zero_flag)
	{
		bitCount = _pHeaderSignedVlcEnc->Encode(_slice._delta_pic_order_cnt[0]);
		if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
		{
			if ((bitsUsedSoFar + bitCount) <= allowedBits)
			{
				if (bsw)
					bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if bitCount...
		else
		{
			strcpy(errInfo, "_delta_pic_order_cnt[0]=");
			sprintf((char *)buff, "%d", _slice._delta_pic_order_cnt[0]);
			strcat(errInfo, buff);
			//strcat(errInfo, itoa(_slice._delta_pic_order_cnt[0], buff, 10));
			goto H264V2_WSLH_VLCERROR_WRITE;
		}//end else...
		bitsUsedSoFar += bitCount;

		if (_picParam[_slice._pic_parameter_set_id]._pic_order_present_flag && !_slice._field_pic_flag)
		{
			bitCount = _pHeaderSignedVlcEnc->Encode(_slice._delta_pic_order_cnt[1]);
			if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
			{
				if ((bitsUsedSoFar + bitCount) <= allowedBits)
				{
					if (bsw)
						bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
				}//end if bitsUsedSoFar...
				else
					goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
			}//end if bitCount...
			else
			{
				strcpy(errInfo, "_delta_pic_order_cnt[1]=");
				sprintf((char *)buff, "%d", _slice._delta_pic_order_cnt[1]);
				strcat(errInfo, buff);
				//strcat(errInfo, itoa(_slice._delta_pic_order_cnt[1], buff, 10));
				goto H264V2_WSLH_VLCERROR_WRITE;
			}//end else...
			bitsUsedSoFar += bitCount;
		}//end if _pic_order_present_flag...
	}//end else if _pic_order_cnt_type...

	if (_picParam[_slice._pic_parameter_set_id]._redundant_pic_cnt_present_flag)
	{
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._redundant_pic_cnt);
		if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
		{
			if ((bitsUsedSoFar + bitCount) <= allowedBits)
			{
				if (bsw)
					bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if bitCount...
		else
		{
			strcpy(errInfo, "_redundant_pic_cnt=");
			sprintf((char *)buff, "%d", _slice._redundant_pic_cnt);
			strcat(errInfo, buff);
			//strcat(errInfo, itoa(_slice._redundant_pic_cnt, buff, 10));
			goto H264V2_WSLH_VLCERROR_WRITE;
		}//end else...
		bitsUsedSoFar += bitCount;
	}//end if _redundant_pic_cnt_present_flag...

	///-------------------------- Direct Spatial Prediction -----------------------------------
	if ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))
	{
		if ((bitsUsedSoFar + 1) <= allowedBits)
		{
			if (bsw)
				bsw->Write(_slice._direct_spatial_mv_pred_flag);
			bitsUsedSoFar++;
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	}//end if B_Slice...

	///-------------------------- Active Reference Indices -----------------------------------
	if ((_slice._type == SliceHeaderH264::P_Slice) || (_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::B_Slice) ||
		(_slice._type == SliceHeaderH264::P_Slice_All) || (_slice._type == SliceHeaderH264::SP_Slice_All) || (_slice._type == SliceHeaderH264::B_Slice_All))
	{
		if ((bitsUsedSoFar + 1) <= allowedBits)
		{
			if (bsw)
				bsw->Write(_slice._num_ref_idx_active_override_flag);
			bitsUsedSoFar++;
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;

		if (_slice._num_ref_idx_active_override_flag)
		{
			bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._num_ref_idx_l0_active_minus1);
			if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
			{
				if ((bitsUsedSoFar + bitCount) <= allowedBits)
				{
					if (bsw)
						bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
				}//end if bitsUsedSoFar...
				else
					goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
			}//end if bitCount...
			else
			{
				strcpy(errInfo, "_num_ref_idx_l0_active_minus1=");
				sprintf((char *)buff, "%d", _slice._num_ref_idx_l0_active_minus1);
				strcat(errInfo, buff);
				//strcat(errInfo, itoa(_slice._num_ref_idx_l0_active_minus1, buff, 10));
				goto H264V2_WSLH_VLCERROR_WRITE;
			}//end else...
			bitsUsedSoFar += bitCount;

			if ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice))
			{
				bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._num_ref_idx_l1_active_minus1);
				if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
				{
					if ((bitsUsedSoFar + bitCount) <= allowedBits)
					{
						if (bsw)
							bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
					}//end if bitsUsedSoFar...
					else
						goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
				}//end if bitCount...
				else
				{
					strcpy(errInfo, "_num_ref_idx_l1_active_minus1=");
					sprintf((char *)buff, "%d", _slice._num_ref_idx_l1_active_minus1);
					strcat(errInfo, buff);
					//strcat(errInfo, itoa(_slice._num_ref_idx_l1_active_minus1, buff, 10));
					goto H264V2_WSLH_VLCERROR_WRITE;
				}//end else...
				bitsUsedSoFar += bitCount;
			}//end if B_Slice...
		}//end if _num_ref_idx_active_override_flag...
	}//end if P_Slice...

	///-------------------------- Reference Pic list reordering --------------------------------
	if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
		(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
	{
		if ((bitsUsedSoFar + 1) <= allowedBits)
		{
			if (bsw)
				bsw->Write(_slice._ref_pic_list_reordering_flag_l0);
			bitsUsedSoFar++;
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;

		if (_slice._ref_pic_list_reordering_flag_l0)
		{
			do
			{
				bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._reordering_of_pic_nums_idc);
				if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
				{
					if ((bitsUsedSoFar + bitCount) <= allowedBits)
					{
						if (bsw)
							bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
					}//end if bitsUsedSoFar...
					else
						goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
				}//end if bitCount...
				else
				{
					strcpy(errInfo, "_reordering_of_pic_nums_idc=");
					sprintf((char *)buff, "%d", _slice._reordering_of_pic_nums_idc);
					strcat(errInfo, buff);
					// strcat(errInfo, itoa(_slice._reordering_of_pic_nums_idc, buff, 10));
					goto H264V2_WSLH_VLCERROR_WRITE;
				}//end else...
				bitsUsedSoFar += bitCount;

				if ((_slice._reordering_of_pic_nums_idc == 0) || (_slice._reordering_of_pic_nums_idc == 1))
				{

					// TODO: Prepare list 0 reordering with _slice._abs_diff_pic_num_minus1.

					bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._abs_diff_pic_num_minus1);
					if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
					{
						if ((bitsUsedSoFar + bitCount) <= allowedBits)
						{
							if (bsw)
								bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
						}//end if bitsUsedSoFar...
						else
							goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
					}//end if bitCount...
					else
					{
						strcpy(errInfo, "_abs_diff_pic_num_minus1=");
						sprintf((char *)buff, "%d", _slice._abs_diff_pic_num_minus1);
						strcat(errInfo, buff);
						//strcat(errInfo, itoa(_slice._abs_diff_pic_num_minus1, buff, 10));
						goto H264V2_WSLH_VLCERROR_WRITE;
					}//end else...
					bitsUsedSoFar += bitCount;
				}//end if _reordering_of_pic_nums_idc...
				else if (_slice._reordering_of_pic_nums_idc == 2)
				{

					// TODO: Prepare list 0 reordering with _slice._long_term_pic_num.

					bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._long_term_pic_num);
					if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
					{
						if ((bitsUsedSoFar + bitCount) <= allowedBits)
						{
							if (bsw)
								bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
						}//end if bitsUsedSoFar...
						else
							goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
					}//end if bitCount...
					else
					{
						strcpy(errInfo, "_long_term_pic_num=");
						sprintf((char *)buff, "%d", _slice._long_term_pic_num);
						strcat(errInfo, buff);
						//strcat(errInfo, itoa(_slice._long_term_pic_num, buff, 10));
						goto H264V2_WSLH_VLCERROR_WRITE;
					}//end else...
					bitsUsedSoFar += bitCount;
				}//end else if _reordering_of_pic_nums_idc...

			} while (_slice._reordering_of_pic_nums_idc != 3);
		}//end if _ref_pic_list_reordering_flag_l0...

	}//end if !I_Slice...

	if ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))
	{
		/// B Slices are not supported for the baseline profile.
		// TODO: B-slice reordering syntax.
		goto H264V2_WSLH_MODEERROR_WRITE;
	}//end if B_Slice...

	///-------------------------- Weighted Prediction Table --------------------------------
	if ((_picParam[_slice._pic_parameter_set_id]._weighted_pred_flag && ((_slice._type == SliceHeaderH264::P_Slice) || (_slice._type == SliceHeaderH264::P_Slice_All) || (_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SP_Slice_All))) ||
		((_picParam[_slice._pic_parameter_set_id]._weighted_bipred_idc == 1) && ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))))
	{
		// TODO: Prediction Weight Table syntax.
		goto H264V2_WSLH_MODEERROR_WRITE;
	}//end if ...

	///-------------------------- Decode Reference Pic Marking  --------------------------------
	if (_nal._ref_idc != 0)
	{
		if (_nal._unit_type == NalHeaderH264::IDR_Slice)
		{
			if ((bitsUsedSoFar + 2) <= allowedBits)
			{
				if (bsw)
				{
					bsw->Write(_slice._no_output_of_prior_pics_flag);
					bsw->Write(_slice._long_term_reference_flag);
				}//end if bsw...
				bitsUsedSoFar += 2;
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if IDR_Slice...
		else
		{
			if ((bitsUsedSoFar + 1) <= allowedBits)
			{
				if (bsw)
					bsw->Write(_slice._adaptive_ref_pic_marking_mode_flag);
				bitsUsedSoFar++;
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;

			if (_slice._adaptive_ref_pic_marking_mode_flag)
			{
				// TODO: Implement Adaptive reference pic marking mode.
				goto H264V2_WSLH_MODEERROR_WRITE;
			}//end if _adaptive_ref_pic_marking_mode_flag...

		}//end else...
	}//end if _ref_idc...

	///-------------------------- CABAC initialisation ----------------------------------
	if (_picParam[_slice._pic_parameter_set_id]._entropy_coding_mode_flag &&
		(_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
		(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
	{
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._cabac_init_idc);
		if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
		{
			if ((bitsUsedSoFar + bitCount) <= allowedBits)
			{
				if (bsw)
					bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if bitCount...
		else
		{
			strcpy(errInfo, "_cabac_init_idc=");
			sprintf((char *)buff, "%d", _slice._cabac_init_idc);
			strcat(errInfo, buff);
			//strcat(errInfo, itoa(_slice._cabac_init_idc, buff, 10));
			goto H264V2_WSLH_VLCERROR_WRITE;
		}//end else...
		bitsUsedSoFar += bitCount;
	}//end if _entropy_coding_mode_flag...

	///-------------------------- Delta Quant Param ---------------------------------------
	bitCount = _pHeaderSignedVlcEnc->Encode(_slice._qp_delta);
	if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
	{
		if ((bitsUsedSoFar + bitCount) <= allowedBits)
		{
			if (bsw)
				bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
		}//end if bitsUsedSoFar...
		else
			goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
	}//end if bitCount...
	else
	{
		strcpy(errInfo, "_qp_delta=");
		sprintf((char *)buff, "%d", _slice._qp_delta);
		strcat(errInfo, buff);
		//strcat(errInfo, itoa(_slice._qp_delta, buff, 10));
		goto H264V2_WSLH_VLCERROR_WRITE;
	}//end else...
	bitsUsedSoFar += bitCount;

	///-------------------------- Switching slices ---------------------------------------
	if ((_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SI_Slice) ||
		(_slice._type == SliceHeaderH264::SP_Slice_All) || (_slice._type == SliceHeaderH264::SI_Slice_All))
	{
		if ((_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SP_Slice_All))
		{
			if ((bitsUsedSoFar + 1) <= allowedBits)
			{
				if (bsw)
					bsw->Write(_slice._sp_for_switch_flag);
				bitsUsedSoFar++;
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if SP_Slice...

		bitCount = _pHeaderSignedVlcEnc->Encode(_slice._qs_delta);
		if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
		{
			if ((bitsUsedSoFar + bitCount) <= allowedBits)
			{
				if (bsw)
					bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if bitCount...
		else
		{
			strcpy(errInfo, "_qs_delta=");
			sprintf((char *)buff, "%d", _slice._qs_delta);
			strcat(errInfo, buff);
			//strcat(errInfo, itoa(_slice._qs_delta, buff, 10));
			goto H264V2_WSLH_VLCERROR_WRITE;
		}//end else...
		bitsUsedSoFar += bitCount;
	}//end if SP_Slice...

	///-------------------------- Deblocking Filter Control -----------------------------------
	if (_picParam[_slice._pic_parameter_set_id]._deblocking_filter_control_present_flag)
	{
		bitCount = _pHeaderUnsignedVlcEnc->Encode(_slice._disable_deblocking_filter_idc);
		if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
		{
			if ((bitsUsedSoFar + bitCount) <= allowedBits)
			{
				if (bsw)
					bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
			}//end if bitsUsedSoFar...
			else
				goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
		}//end if bitCount...
		else
		{
			strcpy(errInfo, "_disable_deblocking_filter_idc=");
			sprintf((char *)buff, "%d", _slice._disable_deblocking_filter_idc);
			strcat(errInfo, buff);
			//strcat(errInfo, itoa(_slice._disable_deblocking_filter_idc, buff, 10));
			goto H264V2_WSLH_VLCERROR_WRITE;
		}//end else...
		bitsUsedSoFar += bitCount;

		if (_slice._disable_deblocking_filter_idc != 1)
		{
			bitCount = _pHeaderSignedVlcEnc->Encode(_slice._alpha_c0_offset_div2);
			if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
			{
				if ((bitsUsedSoFar + bitCount) <= allowedBits)
				{
					if (bsw)
						bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
				}//end if bitsUsedSoFar...
				else
					goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
			}//end if bitCount...
			else
			{
				strcpy(errInfo, "_alpha_c0_offset_div2=");
				sprintf((char *)buff, "%d", _slice._alpha_c0_offset_div2);
				strcat(errInfo, buff);
				//strcat(errInfo, itoa(_slice._alpha_c0_offset_div2, buff, 10));
				goto H264V2_WSLH_VLCERROR_WRITE;
			}//end else...
			bitsUsedSoFar += bitCount;

			bitCount = _pHeaderSignedVlcEnc->Encode(_slice._beta_offset_div2);
			if (bitCount > 0)	///< Vlc codec errors are detected from a zero or negative return value.
			{
				if ((bitsUsedSoFar + bitCount) <= allowedBits)
				{
					if (bsw)
						bsw->Write(bitCount, _pHeaderSignedVlcEnc->GetCode());
				}//end if bitsUsedSoFar...
				else
					goto H264V2_WSLH_RUNOUTOFBITS_WRITE;
			}//end if bitCount...
			else
			{
				strcpy(errInfo, "_beta_offset_div2=");
				sprintf((char *)buff, "%d", _slice._beta_offset_div2);
				strcat(errInfo, buff);
				//strcat(errInfo, itoa(_slice._beta_offset_div2, buff, 10));
				goto H264V2_WSLH_VLCERROR_WRITE;
			}//end else...
			bitsUsedSoFar += bitCount;
		}//end if _disable_deblocking_filter_idc...

	}//end if _deblocking_filter_control_present_flag...

	///-------------------------- Slice Groups ---------------------------------------------
	if ((_picParam[_slice._pic_parameter_set_id]._num_slice_groups_minus1 > 0) &&
		(_picParam[_slice._pic_parameter_set_id]._slice_group_map_type >= 3) &&
		(_picParam[_slice._pic_parameter_set_id]._slice_group_map_type <= 5))
	{
		// TODO: Implement slice group handling.
		goto H264V2_WSLH_MODEERROR_WRITE;

	}//end if _num_slice_groups_minus1...

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_WSLH_RUNOUTOFBITS_WRITE:
	_errorStr = "[H264v2Codec::WriteSliceLayerHeader] Bits required exceeds max available for picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_WSLH_VLCERROR_WRITE:
	strcpy(_errorInfo, "[H264v2Codec::WriteSliceLayerHeader] Vlc encoder error: ");
	strcat(_errorInfo, errInfo);
	_errorStr = _errorInfo;
	*bitsUsed = bitsUsedSoFar;
	return(2);

H264V2_WSLH_MODEERROR_WRITE:
	_errorStr = "[H264v2Codec::WriteSliceLayerHeader] Slice type or mode not implemented";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end WriteSliceLayerHeader.

/** Read the slice layer header from the bit stream.
This impementation reads slice header encoding. Each read checks if a
bit underflow will occur after reading. Check for loss of vlc sync
wherever possible. The checks are sufficiently frequent to warrant
an early exit GOTO statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@return								: Run out of bits = 1, more bits available = 0, error = 2.
*/
int H264v2Codec::ReadSliceLayerHeader(IBitStreamReader* bsr, int remainingBits, int* bitsUsed)
{
	int bitsUsedSoFar = 0;
	int numBits;

	if (bsr == NULL)
	{
		_errorStr = "[H264V2::ReadSliceLayerHeader] No bit stream reader";
		*bitsUsed = 0;
		return(2);
	}//end if !bsr...

	///-------------------------- Addr of first macroblock -----------------------------------
	_slice._first_mb_in_slice = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSLH_NOVLC_READ;
	bitsUsedSoFar += numBits;
	/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSLH_RUNOUTOFBITS_READ;

	///-------------------------- Slice Type -------------------------------------------------
	_slice._type = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSLH_NOVLC_READ;
	bitsUsedSoFar += numBits;
	/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSLH_RUNOUTOFBITS_READ;

	/// Limited implementation of slice types and modes for the Baseline profile.
	if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::I_Slice_All) &&
		(_slice._type != SliceHeaderH264::P_Slice) && (_slice._type != SliceHeaderH264::P_Slice_All))
		goto H264V2_RSLH_NOMODE_READ;

	///-------------------------- Pic Param Set Id --------------------------------------------
	_slice._pic_parameter_set_id = _pHeaderUnsignedVlcDec->Decode(bsr);
	numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSLH_NOVLC_READ;
	bitsUsedSoFar += numBits;
	/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSLH_RUNOUTOFBITS_READ;

	///-------------------------- Frame Number ----------------------------------------------
	numBits = _seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._log2_max_frame_num_minus4 + 4;
	_slice._frame_num = bsr->Read(numBits);
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSLH_RUNOUTOFBITS_READ;

	///-------------------------- Field Flags ----------------------------------------------
	if (!_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._frame_mbs_only_flag)
	{
		_slice._field_pic_flag = bsr->Read();
		bitsUsedSoFar++;
		if (_slice._field_pic_flag)
		{
			_slice._bottom_field_flag = bsr->Read();
			bitsUsedSoFar++;
		}//end if _field_pic_flag...
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;
	}//end if !_frame_mbs_only_flag...

	///-------------------------- IDR Frame Count ------------------------------------------
	if (_nal._unit_type == NalHeaderH264::IDR_Slice)
	{
		_slice._idr_pic_id = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSLH_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;
	}//end if _unit_type...

	///-------------------------- Picture Order Count ---------------------------------------
	if (_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._pic_order_cnt_type == 0)
	{
		numBits = _seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._log2_max_pic_order_cnt_lsb_minus4 + 4;
		_slice._pic_order_cnt_lsb = bsr->Read(numBits);
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;

		if (_picParam[_slice._pic_parameter_set_id]._pic_order_present_flag && !_slice._field_pic_flag)
		{
			_slice._delta_pic_order_cnt_bottom = _pHeaderSignedVlcDec->Decode(bsr);
			numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_RSLH_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;
		}//end if _pic_order_present_flag...

	}//end if _pic_order_cnt_type...
	else if ((_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._pic_order_cnt_type == 1) &&
		!_seqParam[_picParam[_slice._pic_parameter_set_id]._seq_parameter_set_id]._delta_pic_order_always_zero_flag)
	{
		_slice._delta_pic_order_cnt[0] = _pHeaderSignedVlcDec->Decode(bsr);
		numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSLH_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;

		if (_picParam[_slice._pic_parameter_set_id]._pic_order_present_flag && !_slice._field_pic_flag)
		{
			_slice._delta_pic_order_cnt[1] = _pHeaderSignedVlcDec->Decode(bsr);
			numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_RSLH_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;
		}//end if _pic_order_present_flag...
	}//end else if _pic_order_cnt_type...

	if (_picParam[_slice._pic_parameter_set_id]._redundant_pic_cnt_present_flag)
	{
		_slice._redundant_pic_cnt = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSLH_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;
	}//end if _redundant_pic_cnt_present_flag...

	///-------------------------- Direct Spatial Prediction -----------------------------------
	if ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))
	{
		_slice._direct_spatial_mv_pred_flag = bsr->Read();
		bitsUsedSoFar++;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;
	}//end if B_Slice...

	///-------------------------- Active Reference Indices -----------------------------------
	if ((_slice._type == SliceHeaderH264::P_Slice_All) || (_slice._type == SliceHeaderH264::P_Slice) ||
		(_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SP_Slice_All) ||
		(_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))
	{
		_slice._num_ref_idx_active_override_flag = bsr->Read();
		bitsUsedSoFar++;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;

		if (_slice._num_ref_idx_active_override_flag)
		{
			_slice._num_ref_idx_l0_active_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
			numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_RSLH_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;

			if ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))
			{
				_slice._num_ref_idx_l1_active_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
				numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
				if (numBits == 0)	///< Return = 0 implies no valid vlc code.
					goto H264V2_RSLH_NOVLC_READ;
				bitsUsedSoFar += numBits;
				if (bitsUsedSoFar > remainingBits)
					goto H264V2_RSLH_RUNOUTOFBITS_READ;
			}//end if B_Slice...
		}//end if _num_ref_idx_active_override_flag...
	}//end if P_Slice...

	///-------------------------- Reference Pic list reordering --------------------------------
	if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
		(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
	{
		_slice._ref_pic_list_reordering_flag_l0 = bsr->Read();
		bitsUsedSoFar++;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;

		if (_slice._ref_pic_list_reordering_flag_l0)
		{
			do
			{
				_slice._reordering_of_pic_nums_idc = _pHeaderUnsignedVlcDec->Decode(bsr);
				numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
				if (numBits == 0)	///< Return = 0 implies no valid vlc code.
					goto H264V2_RSLH_NOVLC_READ;
				bitsUsedSoFar += numBits;
				if (bitsUsedSoFar > remainingBits)
					goto H264V2_RSLH_RUNOUTOFBITS_READ;

				if ((_slice._reordering_of_pic_nums_idc == 0) || (_slice._reordering_of_pic_nums_idc == 1))
				{
					_slice._abs_diff_pic_num_minus1 = _pHeaderUnsignedVlcDec->Decode(bsr);
					numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
					if (numBits == 0)	///< Return = 0 implies no valid vlc code.
						goto H264V2_RSLH_NOVLC_READ;
					bitsUsedSoFar += numBits;
					if (bitsUsedSoFar > remainingBits)
						goto H264V2_RSLH_RUNOUTOFBITS_READ;

					// TODO: Handle list 0 reordering with _slice._abs_diff_pic_num_minus1.
					goto H264V2_RSLH_NOMODE_READ;

				}//end if _reordering_of_pic_nums_idc...
				else if (_slice._reordering_of_pic_nums_idc == 2)
				{
					_slice._long_term_pic_num = _pHeaderUnsignedVlcDec->Decode(bsr);
					numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
					if (numBits == 0)	///< Return = 0 implies no valid vlc code.
						goto H264V2_RSLH_NOVLC_READ;
					bitsUsedSoFar += numBits;
					if (bitsUsedSoFar > remainingBits)
						goto H264V2_RSLH_RUNOUTOFBITS_READ;

					// TODO: Handle list 0 reordering with _slice._long_term_pic_num.
					goto H264V2_RSLH_NOMODE_READ;

				}//end else if _reordering_of_pic_nums_idc...

			} while (_slice._reordering_of_pic_nums_idc != 3);
		}//end if _ref_pic_list_reordering_flag_l0...
	}//end if !I_Slice...

	if ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))
	{
		/// B Slices are not supported for the baseline profile.
		// TODO: B-slice reordering syntax.
		goto H264V2_RSLH_NOMODE_READ;
	}//end if B_Slice...

	///-------------------------- Weighted Prediction Table --------------------------------
	if ((_picParam[_slice._pic_parameter_set_id]._weighted_pred_flag && ((_slice._type == SliceHeaderH264::P_Slice) || (_slice._type == SliceHeaderH264::P_Slice_All) || (_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SP_Slice_All))) ||
		((_picParam[_slice._pic_parameter_set_id]._weighted_bipred_idc == 1) && ((_slice._type == SliceHeaderH264::B_Slice) || (_slice._type == SliceHeaderH264::B_Slice_All))))
	{
		// TODO: Prediction Weight Table syntax.
		goto H264V2_RSLH_NOMODE_READ;
	}//end if ...

	///-------------------------- Decode Reference Pic Marking  --------------------------------
	if (_nal._ref_idc != 0)
	{
		if (_nal._unit_type == NalHeaderH264::IDR_Slice)
		{
			_slice._no_output_of_prior_pics_flag = bsr->Read();
			_slice._long_term_reference_flag = bsr->Read();
			bitsUsedSoFar += 2;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;
		}//end if IDR_Slice...
		else
		{
			_slice._adaptive_ref_pic_marking_mode_flag = bsr->Read();
			bitsUsedSoFar++;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;

			if (_slice._adaptive_ref_pic_marking_mode_flag)
			{
				// TODO: Implement Adaptive reference pic marking mode.
				goto H264V2_RSLH_NOMODE_READ;
			}//end if _adaptive_ref_pic_marking_mode_flag...

		}//end else...
	}//end if _ref_idc...

	///-------------------------- CABAC initialisation  ---------------------------------------
	if (_picParam[_slice._pic_parameter_set_id]._entropy_coding_mode_flag &&
		(_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
		(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
	{
		_slice._cabac_init_idc = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSLH_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;
	}//end if _entropy_coding_mode_flag...

	///-------------------------- Delta Quant Param ---------------------------------------
	_slice._qp_delta = _pHeaderSignedVlcDec->Decode(bsr);
	numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
	if (numBits == 0)	///< Return = 0 implies no valid vlc code.
		goto H264V2_RSLH_NOVLC_READ;
	bitsUsedSoFar += numBits;
	if (bitsUsedSoFar > remainingBits)
		goto H264V2_RSLH_RUNOUTOFBITS_READ;

	///-------------------------- Switching slices ---------------------------------------
	if ((_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SP_Slice_All) ||
		(_slice._type == SliceHeaderH264::SI_Slice) || (_slice._type == SliceHeaderH264::SI_Slice_All))
	{
		if ((_slice._type == SliceHeaderH264::SP_Slice) || (_slice._type == SliceHeaderH264::SP_Slice_All))
		{
			_slice._sp_for_switch_flag = bsr->Read();
			bitsUsedSoFar++;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;
		}//end if SP_Slice...

		_slice._qs_delta = _pHeaderSignedVlcDec->Decode(bsr);
		numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSLH_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;
	}//end if SP_Slice...

	///-------------------------- Deblocking Filter Control -----------------------------------
	if (_picParam[_slice._pic_parameter_set_id]._deblocking_filter_control_present_flag)
	{
		_slice._disable_deblocking_filter_idc = _pHeaderSignedVlcDec->Decode(bsr);
		numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_RSLH_NOVLC_READ;
		bitsUsedSoFar += numBits;
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RSLH_RUNOUTOFBITS_READ;

		if (_slice._disable_deblocking_filter_idc != 1)
		{
			_slice._alpha_c0_offset_div2 = _pHeaderSignedVlcDec->Decode(bsr);
			numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_RSLH_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;

			_slice._beta_offset_div2 = _pHeaderSignedVlcDec->Decode(bsr);
			numBits = _pHeaderSignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_RSLH_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RSLH_RUNOUTOFBITS_READ;
		}//end if _disable_deblocking_filter_idc...
	}//end if _deblocking_filter_control_present_flag...

	///-------------------------- Slice Groups ---------------------------------------------
	if ((_picParam[_slice._pic_parameter_set_id]._num_slice_groups_minus1 > 0) &&
		(_picParam[_slice._pic_parameter_set_id]._slice_group_map_type >= 3) &&
		(_picParam[_slice._pic_parameter_set_id]._slice_group_map_type <= 5))
	{
		// TODO: Implement slice group handling.
		goto H264V2_RSLH_NOMODE_READ;

	}//end if _num_slice_groups_minus1...

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_RSLH_RUNOUTOFBITS_READ:
	_errorStr = "H264V2:[ReadSliceLayerHeader] Insufficient bits to decode the picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_RSLH_NOVLC_READ:
	_errorStr = "H264V2:[ReadSliceLayerHeader] No valid vlc in bit stream";
	*bitsUsed = bitsUsedSoFar;
	return(2);

H264V2_RSLH_NOMODE_READ:
	_errorStr = "H264V2:[ReadSliceLayerHeader] Slice type or mode not implemented";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end ReadSliceLayerHeader.

/** Write trailing bits to a bit stream.
A stop 1 bit is written and then zero bits are written until a byte boundary. Each
write checks if a bit overflow will occur before writing the stop bit. The zeros
are not counted in the bitsUsed. If the input stream param is NULL then this method
is used to count the bits only.
@param bsw					: Stream to write into.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@return							: Run out of bits = 1, more bits available = 0.
*/
int H264v2Codec::WriteTrailingBits(IBitStreamWriter* bsw, int allowedBits, int* bitsUsed)
{
	int bitsUsedSoFar = 0;

	/// Write the stop bit.
	if (allowedBits < 1)
	{
		_errorStr = "H264V2:[WriteTrailingBits] Run out of bits";
		*bitsUsed = 0;
		return(1);
	}//end if allowedBits...
	if (bsw)
		bsw->Write(1);
	bitsUsedSoFar++;

	/// Write zeros to the end of the byte. Do not count the trailing zeros.
	if (bsw)
	{
		int toByteBoundary = (bsw->GetStreamBitPos() % 8) + 1;
		if (toByteBoundary < 8)  ///< Do not allow the last byte to be 0x00
			bsw->Write(toByteBoundary, 0);
		//bitsUsedSoFar += toByteBoundary;
	}//end if bsw...

	*bitsUsed = bitsUsedSoFar;
	return(0);
}// end WriteTrailingBits.

/** Read trailing bits from the bit stream.
This impementation reads the trailing bit encoding. Each read checks if a
bit underflow will occur after reading. Check for loss of sync
wherever possible. The checks are sufficiently frequent to warrant
an early exit GOTO statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@return								: Run out of bits = 1, more bits available = 0, error = 3.
*/
int H264v2Codec::ReadTrailingBits(IBitStreamReader* bsr, int remainingBits, int* bitsUsed)
{
	int bitsUsedSoFar = 0;
	int bit = 1;

	if (remainingBits < 1)
	{
		_errorStr = "H264V2:[ReadTrailingBits] Run out of bits";
		*bitsUsed = 0;
		return(1);
	}//end if remainingBits...

	/// Read stop bit.
	if (bsr)
		bit = bsr->Read();
	bitsUsedSoFar++;
	if (bit != 1)
	{
		_errorStr = "H264V2:[ReadTrailingBits] No stop bit detected";
		*bitsUsed = bitsUsedSoFar;
		return(3);
	}//end if !stop bit...

	/// Read zeros up to the end of the byte. Do not count the trailing zeros.
	if (bsr)
	{
		int toByteBoundary = (bsr->GetStreamBitPos() % 8) + 1;
		if (toByteBoundary < 8)  ///< Only read zeros within the same byte as the stop bit.
		{
			bit = bsr->Read(toByteBoundary);
			//bitsUsedSoFar += toByteBoundary;
			if (bit != 0)
			{
				_errorStr = "H264V2:[ReadTrailingBits] Missing trailing zeros";
				*bitsUsed = bitsUsedSoFar;
				return(3);
			}//end if !zero bits...
		}//end if toByteBoundary...
	}//end if bsr...

	*bitsUsed = bitsUsedSoFar;
	return(0);
}//end ReadTrailingBits.

/** Insert start code emulation prevention codes.
Scan the entire stream, excluding the 32 bit actual start code, and check
for 24 bit 0x000000 - 0x000003 sequences. Replace them with 32 bit
0x00000300 - 0x00000303 sequences. The extra byte (8 bits) for each
emulation code is excluded from the number of allowed bits but is added
to the total consumed bits. [NB: Care must be taken when checking for
running out of bits to allow for these extra codes.] This method should
only be called once the entire frame has been encoded.
@param bsw	        : Stream to write into.
@param startOffset  : Start evaluating the stream from a byte offset.
@return			        : Return the number of extra bits inserted.
*/
int H264v2Codec::InsertEmulationPrevention(IBitStreamWriter* bsw, int startOffset)
{
	if (bsw == NULL)
		return(0);

	int count = 0;

	unsigned char*  streamHead = (unsigned char*)(bsw->GetStream());
	unsigned char*  stream = &(streamHead[startOffset]);

	/// It is assumed that the stream is fully encoded and the trailing bits have been added
	/// to ensure byte alignement of the stream. Therefore the current stream byte pos is the 
	/// number of encoded bytes in the stream.
	int  endPos = bsw->GetStreamBytePos() - 1 - startOffset;
	if (endPos < 2) return(0);

	/// Find the occurrance of the start code emulation then shift down by copying backwards from the end. Note
	/// that 1st 4 bytes are the start code 0x00000001.
	for (int pos = 6; pos <= endPos; pos++)
	{
		if ((stream[pos] & 0xFC) == 0) ///< Check for 0, 1, 2 or 3.
		{
			if ((stream[pos - 1] == 0) && (stream[pos - 2] == 0)) ///< Check 2 preceeding bytes are zero.
			{
				for (int i = endPos; i >= pos; i--) ///< Shift down by 1 byte.
					stream[i + 1] = stream[i];
				stream[pos] = 0x03; ///< Insert emulation prevention code.
				pos++;              ///< Continue from new shifted position.
				endPos++;           ///< Last byte is now shifted by 1 position.
				count++;
			}//end if stream...
		}//end if stream...
	}//end for pos...

	return(count * 8);
}// end InsertEmulationPrevention.

/** Remove start code emulation prevention codes.
Scan the entire stream and check for 24 bit 0x000003 sequence
and remove the 0x03 byte from the stream. This method should
only be called once before decoding the entire frame.
@param bsr	: Stream to read from.
@return			: Return the number of extra bits removed.
*/
int H264v2Codec::RemoveEmulationPrevention(IBitStreamReader* bsr)
{
	if (bsr == NULL)
		return(0);

	int count = 0;

	unsigned char*  stream = (unsigned char*)(bsr->GetStream());
	int             bits = bsr->GetStreamBitSize();
	int             endPos = (bits / 8) - 1;
	if ((bits % 8) != 0)
		endPos++;

	/// Find the 1st occurrence of the emulation prevention code.
	int pos;
	for (pos = 2; pos <= endPos; pos++)
	{
		if (stream[pos] == 0x03) ///< Emulation prevention code = 0x03...
		{
			if ((stream[pos - 1] == 0) && (stream[pos - 2] == 0))  ///< ...and is preceeded by 2 zero bytes.
			{
				/// Shift remaining bytes up by 1 byte.
				for (int i = pos; i < endPos; i++)
					stream[i] = stream[i + 1];
				pos++;    ///< Skip one to prevent trapping a 0x00 followed by a 0x03 in the actual stream.
				endPos--; ///< Last byte was shifted by 1.
				count++;
			}//end if stream...
		}//end if stream...
	}//end for pos...

	return(count * 8);
}// end RemoveEmulationPrevention.

/** Write the slice data layer to the global bit stream.
The encodings of all the macroblocks must be correctly defined before
this method is called. The vlc encoding is performed first before
writing. This impementation writes every macroblock encoding in top-left
to bottom-right order. Each write checks if a bit overflow will occur
before writing. The check is sufficiently frequent to warrant an early
exit GOTO statement. If the input stream param is NULL then this method is
used to count the bits only.
@param bsw					: Stream to write into.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@return							: Run out of bits = 1, more bits available = 0, Vlc error = 2.
*/
int H264v2Codec::WriteSliceDataLayer(IBitStreamWriter* bsw, int allowedBits, int* bitsUsed)
{
	int mb, i;
	int	bitCount;
	int bitsUsedSoFar = 0;

	/// All macroblocks are written in order.
	int len = _mbLength;
	_mb_skip_run = 0;

	/// ------------------------ Code the slice data -----------------------------------
	for (mb = 0; mb < len; mb++)
	{
		/// Short cut variables.
		MacroBlockH264* pMb = &(_pMb[mb]);

		if (!pMb->_skip)	///< Encoding must set the _skip flag even for Intra macroblocks.
		{
			if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
				(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
			{
				/// ------------------------ Code the skip run -----------------------------------
				bitCount = _pHeaderUnsignedVlcEnc->Encode(_mb_skip_run);
				if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
					goto H264V2_VLCERROR_WRITE;
				if ((bitsUsedSoFar + bitCount) > allowedBits)
					goto H264V2_RUNOUTOFBITS_WRITE;
				if (bsw)
					bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
				bitsUsedSoFar += bitCount;

				_mb_skip_run = 0;	///< ...and reset.
			}//end if !I_Slice...

			/// ------------------------ Code the macroblock data ------------------------------------
			/// Only non-skipped macroblocks call this method.
			int ret = WriteMacroBlockLayer(bsw, pMb, allowedBits - bitsUsedSoFar, &bitCount);
			if (ret) ///< An error has occurred.
			{
				/// _errorStr was set in the WriteMacroBlockLayer() method.
				*bitsUsed = bitsUsedSoFar + bitCount;
				return(ret);
			}//end if ret...
			bitsUsedSoFar += bitCount;
		}//end if !_skip...
		else
		{
			_mb_skip_run++;
			/// Ensure coeffs settings are synchronised for future use by neighbours.
			for (i = 0; i < MBH264_NUM_BLKS; i++)
				pMb->_blkParam[i].pBlk->SetNumCoeffs(0);
		}//end else...

	}//end for mb...

	/// If the final macroblocks of the slice were all skipped then a skip run must be coded onto the stream.
	if (_mb_skip_run)
	{
		if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
			(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
		{
			bitCount = _pHeaderUnsignedVlcEnc->Encode(_mb_skip_run);
			if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
				goto H264V2_VLCERROR_WRITE;
			if ((bitsUsedSoFar + bitCount) > allowedBits)
				goto H264V2_RUNOUTOFBITS_WRITE;
			if (bsw)
				bsw->Write(bitCount, _pHeaderUnsignedVlcEnc->GetCode());
			bitsUsedSoFar += bitCount;
		}//end if !I_Slice...
	}//end if _mb_skip_run...

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_RUNOUTOFBITS_WRITE:
	_errorStr = "H264V2:[WriteSliceDataLayer] Bits required exceeds max available for picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_VLCERROR_WRITE:
	_errorStr = "H264V2:[WriteSliceDataLayer] Vlc encoder error";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end WriteSliceDataLayer.

/** Read the slice data layer from the global bit stream.
This impementation reads every macroblock encoding in top-left to
bottom-right order. Each read checks if a bit underflow will occur
after reading. Check for loss of vlc sync wherever possible. The
checks are sufficiently frequent to warrant an early exit GOTO
statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@return								: Run out of bits = 1, more bits available = 0, error = 2.
*/
int H264v2Codec::ReadSliceDataLayer(IBitStreamReader* bsr, int remainingBits, int* bitsUsed)
{
	int mb, i;
	int bitsUsedSoFar = 0;
	int numBits;

	/// Whip through each macroblock. Extract the encoded macroblock
	/// from the bit stream and vlc decode the vectors and coeff's.
	int len = _mbLength;
	_mb_skip_run = 0;

	/// Get the first skip run from the stream for P slices.
	if ((_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
		(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
	{
		_mb_skip_run = _pHeaderUnsignedVlcDec->Decode(bsr);
		numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
		if (numBits == 0)	///< Return = 0 implies no valid vlc code.
			goto H264V2_NOVLC_READ;
		bitsUsedSoFar += numBits;
		/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
		if (bitsUsedSoFar > remainingBits)
			goto H264V2_RUNOUTOFBITS_READ;
	}//end if !I_Slice...

	for (mb = 0; mb < len; mb++)
	{
		/// Short cut variables.
		MacroBlockH264* pMb = &(_pMb[mb]);

		/// --------------- Decode the macroblock header from the stream ----------------------

		if (_mb_skip_run && (_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
			(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All))
		{
			pMb->_skip = 1;
			_mb_skip_run--;

			/// Skip implies a P_Skip macroblock type as Inter_16x16.
			pMb->_intraFlag = 0;
			pMb->_mb_type = MacroBlockH264::Inter_16x16;
			pMb->_mbPartPredMode = MacroBlockH264::Inter_16x16;
			/// Skip implies a single 16x16 (0,0) motion vector difference.
			pMb->_mvdX[MacroBlockH264::_16x16] = 0;
			pMb->_mvdY[MacroBlockH264::_16x16] = 0;
			/// Set the motion vector to the predicted value.
			if (MacroBlockH264::SkippedZeroMotionPredCondition(pMb))
			{
				/// Force to zero.
				pMb->_mvX[MacroBlockH264::_16x16] = 0;
				pMb->_mvY[MacroBlockH264::_16x16] = 0;
			}//end if SkippedZeroMotionPredCondition...
			else
				pMb->GetMbMotionMedianPred(pMb, &(pMb->_mvX[MacroBlockH264::_16x16]), &(pMb->_mvY[MacroBlockH264::_16x16]));
			pMb->_coded_blk_pattern = 0;
			/// Ensure coeffs settings are synchronised for future use by neighbours.
			for (i = 0; i < MBH264_NUM_BLKS; i++)
				pMb->_blkParam[i].pBlk->SetNumCoeffs(0);
		}//end if _mb_skip_run...
		else
		{
			/// Macroblock not skipped and header must be extracted.
			pMb->_skip = 0;

			/// Macroblock type.
			_pMb[mb]._mb_type = _pMbTypeVlcDec->Decode(bsr);
			numBits = _pMbTypeVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_NOVLC_READ;
			bitsUsedSoFar += numBits;
			/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RUNOUTOFBITS_READ;
			/// Unpack _intraFlag and _mbPartPredMode from the decoded _mb_type.
			_pMb[mb].UnpackMbType(&(_pMb[mb]), _slice._type);

			/// Intra requires lum and chr prediction modes, Inter requires reference 
			/// index lists and motion vector diff values.
			if (!_pMb[mb]._intraFlag)	///< Inter	(most common option)
			{
				int numOfVecs = 1;
				// TODO: Implement other partition modes.
				if (_pMb[mb]._mbPartPredMode == MacroBlockH264::Inter_16x16)
					numOfVecs = 1;
				else
					goto H264V2_NOMODE_READ;
				for (int vec = 0; vec < numOfVecs; vec++)
				{
					/// Get the motion vector differences for this macroblock.
					_pMb[mb]._mvdX[vec] = _pMbMotionVecDiffVlcDec->Decode(bsr);
					numBits = _pMbMotionVecDiffVlcDec->GetNumDecodedBits();
					if (numBits == 0)
						goto H264V2_NOVLC_READ;
					bitsUsedSoFar += numBits;

					_pMb[mb]._mvdY[vec] = _pMbMotionVecDiffVlcDec->Decode(bsr);
					numBits = _pMbMotionVecDiffVlcDec->GetNumDecodedBits();
					if (numBits == 0)
						goto H264V2_NOVLC_READ;
					bitsUsedSoFar += numBits;
					if (bitsUsedSoFar > remainingBits)
						goto H264V2_RUNOUTOFBITS_READ;

					/// Get the prediction vector from the neighbourhood.
					int predX, predY;
					_pMb[mb].GetMbMotionMedianPred(&(_pMb[mb]), &predX, &predY);
					_pMb[mb]._mvX[vec] = predX + _pMb[mb]._mvdX[vec];
					_pMb[mb]._mvY[vec] = predY + _pMb[mb]._mvdY[vec];
				}//end for vec...
			}//end if !_interFlag...
			else											/// Intra
			{
				// TODO: Implement Intra_8x8 and Intra_4x4 mode options.

				/// Get chr prediction mode.
				_pMb[mb]._intraChrPredMode = _pMbIChrPredModeVlcDec->Decode(bsr);
				numBits = _pMbIChrPredModeVlcDec->GetNumDecodedBits();
				if (numBits == 0)
					goto H264V2_NOVLC_READ;
				bitsUsedSoFar += numBits;
				if (bitsUsedSoFar > remainingBits)
					goto H264V2_RUNOUTOFBITS_READ;
			}//end else...

			/// If not Intra_16x16 mode then _coded_blk_pattern must be extracted.
			if ((_pMb[mb]._intraFlag && (_pMb[mb]._mbPartPredMode != MacroBlockH264::Intra_16x16)) || (!_pMb[mb]._intraFlag))
			{
				/// Block coded pattern.
				int isInter = 1;
				if (_pMb[mb]._intraFlag) isInter = 0;
				numBits = _pBlkPattVlcDec->Decode2(bsr, &(_pMb[mb]._coded_blk_pattern), &isInter);
				if (numBits == 0)
					goto H264V2_NOVLC_READ;
				bitsUsedSoFar += numBits;
				if (bitsUsedSoFar > remainingBits)
					goto H264V2_RUNOUTOFBITS_READ;
			}//end if !Intra_16x16...

		}//end else not skipped...

		/// Disassemble the bit pattern to determine which blocks are to be decoded.
		_pMb[mb].GetCodedBlockPattern(&(_pMb[mb]));

		/// For Inter modes and non-Intra_16x16 modes, delta qp is read only if there is at least one block 
		/// that has coeffs. The all zero coeff condition still requires the pattern to be decoded such that 
		/// zeros are loaded into the blocks.
		_pMb[mb]._mb_qp_delta = 0;	///< Default value required.
		if ((_pMb[mb]._coded_blk_pattern > 0) || (_pMb[mb]._intraFlag && (_pMb[mb]._mbPartPredMode == MacroBlockH264::Intra_16x16)))
		{
			/// Delta QP.
			_pMb[mb]._mb_qp_delta = _pDeltaQPVlcDec->Decode(bsr);
			numBits = _pDeltaQPVlcDec->GetNumDecodedBits();
			if (numBits == 0)
				goto H264V2_NOVLC_READ;
			bitsUsedSoFar += numBits;
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RUNOUTOFBITS_READ;
		}//end if _coded_blk_pattern...

		int prevMbIdx = _pMb[mb]._mbIndex - 1;
		if (prevMbIdx >= 0)	///< Previous macroblock is within the image boundaries.
		{
			if (_pMb[mb]._slice == _pMb[prevMbIdx]._slice)	/// Previous macroblock within same slice.
				_pMb[mb]._mbQP = _pMb[prevMbIdx]._mbQP + _pMb[mb]._mb_qp_delta;
			else
				_pMb[mb]._mbQP = _slice._qp + _pMb[mb]._mb_qp_delta;
		}//end if prevMbIdx...
		else
			_pMb[mb]._mbQP = _slice._qp + _pMb[mb]._mb_qp_delta;

		/// ------------------- Get the macroblock coeffs from the coded stream ---------------------
		int dcSkip = 0;
		int startBlk = 1;
		if (_pMb[mb]._intraFlag && (_pMb[mb]._mbPartPredMode == MacroBlockH264::Intra_16x16))
		{
			startBlk = 0;	///< Change starting block to include block num = -1;
			dcSkip = 1;
		}//end if Intra_16x16...
		/// For Lum blocks that are Inter coded and not Intra_16x16 coded the DC coeff is not skipped.
		for (i = MBH264_LUM_0_0; i <= MBH264_LUM_3_3; i++)
			_pMb[mb]._blkParam[i].dcSkipFlag = dcSkip;

		for (i = startBlk; i < MBH264_NUM_BLKS; i++)
		{
			/// Simplify the block reference.
			BlockH264* pBlk = _pMb[mb]._blkParam[i].pBlk;

			if (pBlk->IsCoded())
			{
				/// Choose the appropriate dimension CAVLC codec.
				IContextAwareRunLevelCodec* pCAVLC = _pCAVLC2x2;
				if ((pBlk->GetHeight() == 4) && (pBlk->GetWidth() == 4))
					pCAVLC = _pCAVLC4x4;

				/// Get num of neighbourhood coeffs as average of above and left block coeffs. Previous
				/// MB decodings in decoding order have already set the num of neighbourhood coeffs.
				int neighCoeffs = 0;
				if (_pMb[mb]._blkParam[i].neighbourIndicator)
				{
					if (_pMb[mb]._blkParam[i].neighbourIndicator > 0)
						neighCoeffs = BlockH264::GetNumNeighbourCoeffs(pBlk);
					else	///< Negative values for neighbourIndicator imply pass through.
						neighCoeffs = _pMb[mb]._blkParam[i].neighbourIndicator;
				}//end if neighbourIndicator...
				pCAVLC->SetParameter(pCAVLC->NUM_TOT_NEIGHBOR_COEFF_ID, neighCoeffs);	///< Prepare the vlc coder.
				pCAVLC->SetParameter(pCAVLC->DC_SKIP_FLAG_ID, _pMb[mb]._blkParam[i].dcSkipFlag);

				numBits = pBlk->RleDecode(pCAVLC, bsr);					///< Vlc decode from the stream.
				if (numBits <= 0)	///< Vlc codec errors are detected from a negative return value.
				{
					if (numBits == -2)
						goto H264V2_RUNOUTOFBITS_READ;
					goto H264V2_NOVLC_READ;
				}//end if numBits...
				bitsUsedSoFar += numBits;
				if (bitsUsedSoFar > remainingBits)
					goto H264V2_RUNOUTOFBITS_READ;
			}//end if IsCoded()...
			else
			{
				pBlk->SetNumCoeffs(0);	///< For future use.
				pBlk->Zero();
			}//end else...
		}//end for i...

		/// If end of skipped macroblocks then get the next skip run from the stream.
		if (!_mb_skip_run && !pMb->_skip && (_slice._type != SliceHeaderH264::I_Slice) && (_slice._type != SliceHeaderH264::SI_Slice) &&
			(_slice._type != SliceHeaderH264::I_Slice_All) && (_slice._type != SliceHeaderH264::SI_Slice_All) && (mb != (len - 1)))
		{
			_mb_skip_run = _pHeaderUnsignedVlcDec->Decode(bsr);
			numBits = _pHeaderUnsignedVlcDec->GetNumDecodedBits();
			if (numBits == 0)	///< Return = 0 implies no valid vlc code.
				goto H264V2_NOVLC_READ;
			bitsUsedSoFar += numBits;
			/// Vlc length unknown and so only checked after the fact but assumes non-destructive read.
			if (bitsUsedSoFar > remainingBits)
				goto H264V2_RUNOUTOFBITS_READ;
		}//end if !_mb_skip_run...

	}//end for mb...

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_RUNOUTOFBITS_READ:
	_errorStr = "H264V2:[ReadSliceDataLayer] Insufficient bits to decode the picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_NOVLC_READ:
	_errorStr = "H264V2:[ReadSliceDataLayer] No valid vlc in bit stream";
	*bitsUsed = bitsUsedSoFar;
	return(2);

H264V2_NOMODE_READ:
	_errorStr = "H264V2:[ReadSliceDataLayer] Mode not supported";
	*bitsUsed = bitsUsedSoFar;
	return(3);
}//end ReadSliceDataLayer.

/** Write the macroblock layer to the global bit stream.
The encodings of all the macroblocks must be correctly defined before
this method is called. The vlc encoding is performed first before
writing. This impementation writes one non-skipped macroblock encoding.
Each write checks if a bit overflow will occur before writing. The check
is sufficiently frequent to warrant an early exit GOTO statement. If the
input stream param is NULL then this method is used to count the bits only.
@param bsw					: Stream to write into.
@param pMb					: Macroblock to encode.
@param allowedBits	: Upper limit to the writable bits.
@param bitsUsed			: Return the actual bits used.
@return							: Run out of bits = 1, more bits available = 0, Vlc error = 2.
*/
int H264v2Codec::WriteMacroBlockLayer(IBitStreamWriter* bsw, MacroBlockH264* pMb, int allowedBits, int* bitsUsed)
{
	int	bitCount, i;
	int bitsUsedSoFar = 0;
	int dcSkip = 0;
	int startBlk = 1;

	/// Encode the vlc blocks with the context of the neighborhood number of coeffs. Map
	/// to the required H.264 coding order of each block when writing to the stream.

	/// ------------------------ Code the macroblock header -----------------------------------

	/// Macroblock type.
	bitCount = _pMbTypeVlcEnc->Encode(pMb->_mb_type);
	if (bitCount <= 0)	///< Vlc codec errors are detected from a zero or negative return value.
		goto H264V2_VLCERROR_WRITE_MB;
	if ((bitsUsedSoFar + bitCount) > allowedBits)
		goto H264V2_RUNOUTOFBITS_WRITE_MB;
	if (bsw)
		bsw->Write(bitCount, _pMbTypeVlcEnc->GetCode());
	bitsUsedSoFar += bitCount;

	/// Intra requires lum and chr prediction modes, Inter requires reference 
	/// index lists and motion vector diff values.
	if (!pMb->_intraFlag)	/// Inter	(most common option)
	{
		int numOfVecs = 1;
		if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
			numOfVecs = 1;
		for (int vec = 0; vec < numOfVecs; vec++)
		{
			bitCount = _pMbMotionVecDiffVlcEnc->Encode(pMb->_mvdX[vec]);
			if (bitCount <= 0)
				goto H264V2_VLCERROR_WRITE_MB;
			if ((bitsUsedSoFar + bitCount) > allowedBits)
				goto H264V2_RUNOUTOFBITS_WRITE_MB;
			if (bsw)
				bsw->Write(bitCount, _pMbMotionVecDiffVlcEnc->GetCode());
			bitsUsedSoFar += bitCount;

			bitCount = _pMbMotionVecDiffVlcEnc->Encode(pMb->_mvdY[vec]);
			if (bitCount <= 0)
				goto H264V2_VLCERROR_WRITE_MB;
			if ((bitsUsedSoFar + bitCount) > allowedBits)
				goto H264V2_RUNOUTOFBITS_WRITE_MB;
			if (bsw)
				bsw->Write(bitCount, _pMbMotionVecDiffVlcEnc->GetCode());
			bitsUsedSoFar += bitCount;

		}//end for vec...
	}//end if !_intraFlag...
	else											/// Intra
	{
		// TODO: Implement Intra_8x8 and Intra_4x4 mode options.

		/// Write chr prediction mode.
		bitCount = _pMbIChrPredModeVlcEnc->Encode(pMb->_intraChrPredMode);
		if (bitCount <= 0)
			goto H264V2_VLCERROR_WRITE_MB;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_RUNOUTOFBITS_WRITE_MB;
		if (bsw)
			bsw->Write(bitCount, _pMbIChrPredModeVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

	}//end else...

	/// If not Intra_16x16 mode then _coded_blk_pattern must be written.
	if ((pMb->_intraFlag && (pMb->_mbPartPredMode != MacroBlockH264::Intra_16x16)) || (!pMb->_intraFlag))
	{
		/// Block coded pattern.
		int isInter = 1;
		if (pMb->_intraFlag) isInter = 0;
		bitCount = _pBlkPattVlcEnc->Encode2(pMb->_coded_blk_pattern, isInter);
		if (bitCount <= 0)
			goto H264V2_VLCERROR_WRITE_MB;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_RUNOUTOFBITS_WRITE_MB;
		if (bsw)
			bsw->Write(bitCount, _pBlkPattVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

	}//end if !Intra_16x16...

	/// For Inter modes and non-Intra_16x16 modes, delta qp and the residual coeffs are written only if 
	/// there is at least one block that has coeffs. 
	if ((pMb->_coded_blk_pattern > 0) || (pMb->_intraFlag && (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)))
	{
		/// Delta QP.
		bitCount = _pDeltaQPVlcEnc->Encode(pMb->_mb_qp_delta);
		if (bitCount <= 0)
			goto H264V2_VLCERROR_WRITE_MB;
		if ((bitsUsedSoFar + bitCount) > allowedBits)
			goto H264V2_RUNOUTOFBITS_WRITE_MB;
		if (bsw)
			bsw->Write(bitCount, _pDeltaQPVlcEnc->GetCode());
		bitsUsedSoFar += bitCount;

	}//end if _coded_blk_pattern...

	/// ------------------ Code the macroblock data --------------------------------------------------------
	if ((pMb->_intraFlag) && (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16))
	{
		startBlk = 0;	///< Change starting block to include block num = -1;
		dcSkip = 1;
	}//end if Intra_16x16...
	/// For Lum blocks that are Inter coded or not Intra_16x16 coded, the DC coeff is not skipped.
	for (i = MBH264_LUM_0_0; i <= MBH264_LUM_3_3; i++)
		pMb->_blkParam[i].dcSkipFlag = dcSkip;

	for (i = startBlk; i < MBH264_NUM_BLKS; i++)
	{
		/// Simplify the block reference.
		BlockH264* pBlk = pMb->_blkParam[i].pBlk;

		if (pMb->_blkParam[i].pBlk->IsCoded())
		{
			/// Choose the appropriate dimension CAVLC codec. Only 4x4 or 2x2 are expected here.
			IContextAwareRunLevelCodec* pCAVLC = _pCAVLC2x2;
			if (((pMb->_blkParam[i].pBlk)->GetHeight() == 4) && ((pMb->_blkParam[i].pBlk)->GetWidth() == 4))
				pCAVLC = _pCAVLC4x4;

			/// Get num of neighbourhood coeffs as average of above and left block coeffs. Previous
			/// MB encodings in raster order have already set the num of coeffs.
			int neighCoeffs = 0;
			if (pMb->_blkParam[i].neighbourIndicator)
			{
				if (pMb->_blkParam[i].neighbourIndicator > 0)
					neighCoeffs = BlockH264::GetNumNeighbourCoeffs(pMb->_blkParam[i].pBlk);
				else	///< Negative values for neighbourIndicator imply pass through.
					neighCoeffs = pMb->_blkParam[i].neighbourIndicator;
			}//end if neighbourIndicator...
			pCAVLC->SetParameter(pCAVLC->NUM_TOT_NEIGHBOR_COEFF_ID, neighCoeffs);	///< Prepare the vlc coder.
			pCAVLC->SetParameter(pCAVLC->DC_SKIP_FLAG_ID, pMb->_blkParam[i].dcSkipFlag);

			///< Vlc encode and add to stream. The number of coeffs is set here for the blk.
			bitCount = (pMb->_blkParam[i].pBlk)->RleEncode(pCAVLC, bsw);
			if (bitCount <= 0)
			{
				if (bitCount == -2)
					goto H264V2_RUNOUTOFBITS_WRITE_MB;
				goto H264V2_VLCERROR_WRITE_MB;
			}//end if bitCount...
			if ((bitsUsedSoFar + bitCount) > allowedBits)
				goto H264V2_RUNOUTOFBITS_WRITE_MB;
			bitsUsedSoFar += bitCount;
		}//end if IsCoded()...
		else
			pMb->_blkParam[i].pBlk->SetNumCoeffs(0);	///< For future use.
	}//end for i...

	*bitsUsed = bitsUsedSoFar;
	return(0);

H264V2_RUNOUTOFBITS_WRITE_MB:
	/// Error info has contents loaded from outside of this method therefore concat is required.
	//strcat(_errorInfo, " H264V2:[WriteMacroBlockLayer] Bits required exceeds max available for picture");
	//_errorStr = _errorInfo;
	_errorStr = "H264V2:[WriteMacroBlockLayer] Bits required exceeds max available for picture";
	*bitsUsed = bitsUsedSoFar;
	return(1);

H264V2_VLCERROR_WRITE_MB:
	_errorStr = "H264V2:[WriteMacroBlockLayer] Vlc encoder error";
	*bitsUsed = bitsUsedSoFar;
	return(2);

}//end WriteMacroBlockLayer.

/** Count the bits required to encode the macroblock layer.
The encodings of all the macroblocks must be correctly defined before
this method is called. This impementation counts the bits required for
one non-skipped macroblock encoding. NB: No errors are checked.
@param pMb					: Macroblock to encode.
@return							: Total bit count.
*/
int H264v2Codec::MacroBlockLayerBitCounter(MacroBlockH264* pMb)
{
	int	i;
	int bitsUsedSoFar = 0;

	/// Encode the vlc blocks with the context of the neighborhood number of coeffs. Map
	/// to the required H.264 coding order of each block when writing to the stream.

	/// ------------------------ Code the macroblock header -----------------------------------

	/// Macroblock type.
	bitsUsedSoFar = _pMbTypeVlcEnc->Encode(pMb->_mb_type);

	/// Intra requires lum and chr prediction modes, Inter requires reference 
	/// index lists and motion vector diff values.
	if (!pMb->_intraFlag)	/// Inter	(most common option)
	{
		int numOfVecs = 1;
		if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
			numOfVecs = 1;
		for (int vec = 0; vec < numOfVecs; vec++)
		{
			bitsUsedSoFar += _pMbMotionVecDiffVlcEnc->Encode(pMb->_mvdX[vec]);
			bitsUsedSoFar += _pMbMotionVecDiffVlcEnc->Encode(pMb->_mvdY[vec]);
		}//end for vec...
	}//end if !_intraFlag...
	else									/// Intra
	{
		// TODO: Implement Intra_8x8 and Intra_4x4 mode options.

		/// Write chr prediction mode.
		bitsUsedSoFar += _pMbIChrPredModeVlcEnc->Encode(pMb->_intraChrPredMode);
	}//end else...

	/// If not Intra_16x16 mode then _coded_blk_pattern must be written.
	if ((pMb->_intraFlag && (pMb->_mbPartPredMode != MacroBlockH264::Intra_16x16)) || (!pMb->_intraFlag))
	{
		/// Block coded pattern.
		int isInter = 1;
		if (pMb->_intraFlag) isInter = 0;
		bitsUsedSoFar += _pBlkPattVlcEnc->Encode2(pMb->_coded_blk_pattern, isInter);
	}//end if !Intra_16x16...

	/// For Inter modes and non-Intra_16x16 modes, delta qp and the residual coeffs are written only if 
	/// there is at least one block that has coeffs. 
	if ((pMb->_coded_blk_pattern > 0) || (pMb->_intraFlag && (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)))
	{
		/// Delta QP.
		bitsUsedSoFar += _pDeltaQPVlcEnc->Encode(pMb->_mb_qp_delta);
	}//end if _coded_blk_pattern...

	/// ------------------ Code the macroblock data --------------------------------------------------------
	int dcSkip = 0;
	int startBlk = 1;
	if ((pMb->_intraFlag) && (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16))
	{
		startBlk = 0;	///< Change starting block to include block num = -1;
		dcSkip = 1;
	}//end if Intra_16x16...
	/// For Lum blocks that are Inter coded or not Intra_16x16 coded, the DC coeff is not skipped.
	for (i = MBH264_LUM_0_0; i <= MBH264_LUM_3_3; i++)
		pMb->_blkParam[i].dcSkipFlag = dcSkip;

	for (i = startBlk; i < MBH264_NUM_BLKS; i++)
	{
		/// Simplify the block reference.
		BlockH264* pBlk = pMb->_blkParam[i].pBlk;

		if (pMb->_blkParam[i].pBlk->IsCoded())
		{
			/// Choose the appropriate dimension CAVLC codec. Only 4x4 or 2x2 are expected here.
			IContextAwareRunLevelCodec* pCAVLC = _pCAVLC2x2;
			if (((pMb->_blkParam[i].pBlk)->GetHeight() == 4) && ((pMb->_blkParam[i].pBlk)->GetWidth() == 4))
				pCAVLC = _pCAVLC4x4;

			/// Get num of neighbourhood coeffs as average of above and left block coeffs. Previous
			/// MB encodings in raster order have already set the num of coeffs.
			int neighCoeffs = 0;
			if (pMb->_blkParam[i].neighbourIndicator)
			{
				if (pMb->_blkParam[i].neighbourIndicator > 0)
					neighCoeffs = BlockH264::GetNumNeighbourCoeffs(pMb->_blkParam[i].pBlk);
				else	///< Negative values for neighbourIndicator imply pass through.
					neighCoeffs = pMb->_blkParam[i].neighbourIndicator;
			}//end if neighbourIndicator...
			pCAVLC->SetParameter(pCAVLC->NUM_TOT_NEIGHBOR_COEFF_ID, neighCoeffs);	///< Prepare the vlc coder.
			pCAVLC->SetParameter(pCAVLC->DC_SKIP_FLAG_ID, pMb->_blkParam[i].dcSkipFlag);

			///< Vlc encode and add to stream. The number of coeffs is set here for the blk.
			bitsUsedSoFar += (pMb->_blkParam[i].pBlk)->RleEncode(pCAVLC, NULL);
		}//end if IsCoded()...
		else
			pMb->_blkParam[i].pBlk->SetNumCoeffs(0);	///< For future use.
	}//end for i...

	return(bitsUsedSoFar);
}//end MacroBlockLayerBitCounter.

/** Count the bits required to encode the coeffs at the macroblock layer.
The encodings of all the macroblocks must be correctly defined before
this method is called. This impementation counts the bits required for
one non-skipped macroblock encoding excluding the headers. NB: No errors
are checked.
@param pMb	: Macroblock to encode.
@return			: Total bit count.
*/
int H264v2Codec::MacroBlockLayerCoeffBitCounter(MacroBlockH264* pMb)
{
	int	i;
	int bitsUsedSoFar = 0;

	/// Encode the vlc blocks with the context of the neighborhood number of coeffs. Ignore
  /// the header.

	int dcSkip = 0;
	int startBlk = 1;
	if ((pMb->_intraFlag) && (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16))
	{
		startBlk = 0;	///< Change starting block to include block num = -1;
		dcSkip = 1;
	}//end if Intra_16x16...

	/// For Lum blocks that are Inter coded or not Intra_16x16 coded, the DC coeff is not skipped.
	for (i = MBH264_LUM_0_0; i <= MBH264_LUM_3_3; i++)
		pMb->_blkParam[i].dcSkipFlag = dcSkip;

	for (i = startBlk; i < MBH264_NUM_BLKS; i++)
	{
		/// Simplify the block reference.
		BlockH264* pBlk = pMb->_blkParam[i].pBlk;

		if (pMb->_blkParam[i].pBlk->IsCoded())
		{
			/// Choose the appropriate dimension CAVLC codec. Only 4x4 or 2x2 are expected here.
			IContextAwareRunLevelCodec* pCAVLC = _pCAVLC2x2;
			if (((pMb->_blkParam[i].pBlk)->GetHeight() == 4) && ((pMb->_blkParam[i].pBlk)->GetWidth() == 4))
				pCAVLC = _pCAVLC4x4;

			/// Get num of neighbourhood coeffs as average of above and left block coeffs. Previous
			/// MB encodings in raster order have already set the num of coeffs.
			int neighCoeffs = 0;
			if (pMb->_blkParam[i].neighbourIndicator)
			{
				if (pMb->_blkParam[i].neighbourIndicator > 0)
					neighCoeffs = BlockH264::GetNumNeighbourCoeffs(pMb->_blkParam[i].pBlk);
				else	///< Negative values for neighbourIndicator imply pass through.
					neighCoeffs = pMb->_blkParam[i].neighbourIndicator;
			}//end if neighbourIndicator...
			pCAVLC->SetParameter(pCAVLC->NUM_TOT_NEIGHBOR_COEFF_ID, neighCoeffs);	///< Prepare the vlc coder.
			pCAVLC->SetParameter(pCAVLC->DC_SKIP_FLAG_ID, pMb->_blkParam[i].dcSkipFlag);

			///< Vlc encode and accumulate the bit count. The number of coeffs is set here for the blk.
			bitsUsedSoFar += (pMb->_blkParam[i].pBlk)->RleEncode(pCAVLC, NULL);
		}//end if IsCoded()...
		else
			pMb->_blkParam[i].pBlk->SetNumCoeffs(0);	///< For future use.
	}//end for i...

	return(bitsUsedSoFar);
}//end MacroBlockLayerCoeffBitCounter.

/** Read the macroblock layer from the global bit stream.
This impementation reads every macroblock encoding in top-left to
bottom-right order. Each read checks if a bit underflow will occur
after reading. Check for loss of vlc sync wherever possible. The
checks are sufficiently frequent to warrant an early exit GOTO
statement.
@param bsr						: Stream to read from.
@param remainingBits	: Upper limit to the readable bits.
@param bitsUsed				: Return the actual bits extracted.
@return								: Run out of bits = 1, more bits available = 0, error = 2.
*/
int H264v2Codec::ReadMacroBlockLayer(IBitStreamReader* bsr, int remainingBits, int* bitsUsed)
{
	int bitsUsedSoFar = 0;

	*bitsUsed = bitsUsedSoFar;
	return(0);
}//end ReadMacroBlockLayer.

/** Apply the in-loop edge filter.
Used in both the encoder and decoder to remove blocking artefacts on the 4x4 boundary
edges. It is applied macroblock by macroblock in raster scan order to the reference
images of both the Lum and Chr components.
@return	:	none.
*/
void H264v2Codec::ApplyLoopFilter(void)
{
	int mb, i, j;
	short** lumRef = _RefLum->Get2DSrcPtr();	///< Image space to operate on.
	short** cbRef = _RefCb->Get2DSrcPtr();
	short** crRef = _RefCr->Get2DSrcPtr();

	for (mb = 0; mb < _mbLength; mb++)
	{
		MacroBlockH264* pMb = &(_pMb[mb]);
		MacroBlockH264* aboveMb = _pMb[mb]._aboveMb;
		MacroBlockH264* leftMb = _pMb[mb]._leftMb;

		/// All macroblock boundaries that have intra neighbours use
		/// boundary strength = {3, 4}. Vertical filtering first.

		///---------------- Vertical Edges --------------------------------------
		if (leftMb != NULL)	///< Only look at macroblock boundary if there is a neighbour.
		{
			if (pMb->_intraFlag || leftMb->_intraFlag)	///< Left intra macroblock boundary (bS = 4).
			{
				for (i = 0; i < 16; i += 4)
					VerticalFilter(pMb, lumRef, 1, i, 0, 4, 4);
				for (i = 0; i < 8; i += 4)
				{
					VerticalFilter(pMb, cbRef, 0, i, 0, 4, 4);
					VerticalFilter(pMb, crRef, 0, i, 0, 4, 4);
				}//end for i...
			}//end if _intraFlag...
			else																	///< Left inter macroblock boundary.
			{
				// TODO: For this current implementation only one 16x16 vector is used
				// per macroblock and from the same single reference. Boundary 4x4 blocks 
		// are compared with the neighbouring macroblock motion vectors.

				int mvDiffersBy4 = 0;	///< Differ with neighbour by 4 quarter pel values.
				if ((H264V2_FAST_ABS32(pMb->_mvX[0] - leftMb->_mvX[0]) >= 4) || (H264V2_FAST_ABS32(pMb->_mvY[0] - leftMb->_mvY[0]) >= 4))
					mvDiffersBy4 = 1;

				for (i = 0; i < 4; i++)
				{
					int bS = mvDiffersBy4;
					if (pMb->_lumBlk[i][0].GetNumCoeffs() || pMb->_lumBlk[i][0]._blkLeft->GetNumCoeffs())	///< Coded coeffs in block with q or block with p.
						bS = 2;

					if (bS)
					{
						/// Apply the filter to this macroblock block boundary.
						VerticalFilter(pMb, lumRef, 1, i << 2, 0, 4, bS);	///< At (row = 4*i, col = 0) do iter = 4 rows.

						/// Apply to the aligned chr edge assuming 4:2:0 here only.
						VerticalFilter(pMb, cbRef, 0, i << 1, 0, 2, bS);	///< At (row = 2*i, col = 0) do iter = 2 rows.
						VerticalFilter(pMb, crRef, 0, i << 1, 0, 2, bS);	///< At (row = 2*i, col = 0) do iter = 2 rows.
					}//end if bS...
				}//end for i...

			}//end else...
		}//end if leftMb...

		if (pMb->_intraFlag)	///< Internal intra block edges.
		{
			for (j = 4; j < 16; j += 4)
				for (i = 0; i < 16; i += 4)	///< All rows first for each col.
					VerticalFilter(pMb, lumRef, 1, i, j, 4, 3);
			for (j = 4; j < 8; j += 4)
				for (i = 0; i < 8; i += 4)
				{
					VerticalFilter(pMb, cbRef, 0, i, j, 4, 3);
					VerticalFilter(pMb, crRef, 0, i, j, 4, 3);
				}//end for j & i...
		}//end if _intraFlag...
		else										///< Internal inter block edges.
		{
			// TODO: For this current implementation only one 16x16 motion vector is used
			// per macroblock and from the same single reference. Therefore all internal
			// blocks have the same motion vector i.e. difference = 0.

			for (j = 1; j < 4; j++)
				for (i = 0; i < 4; i++)
				{
					int bS = 0;
					if (pMb->_lumBlk[i][j].GetNumCoeffs() || pMb->_lumBlk[i][j]._blkLeft->GetNumCoeffs())	///< Coded coeffs in block with q or block with p.
						bS = 2;

					if (bS)
					{
						/// Apply the filter to this block boundary.
						VerticalFilter(pMb, lumRef, 1, i << 2, j << 2, 4, bS);	///< At (row = 4*i, col = 4*j) do iter = 4 rows.

						/// Apply to the aligned chr edge assuming 4:2:0 here only.
						if (j == 2)
						{
							VerticalFilter(pMb, cbRef, 0, i << 1, j << 1, 2, bS);	///< At (row = 2*i, col = 2*j) do iter = 2 rows.
							VerticalFilter(pMb, crRef, 0, i << 1, j << 1, 2, bS);	///< At (row = 2*i, col = 2*j) do iter = 2 rows.
						}//end if j...
					}//end if bS...

				}//end for j & i...
		}//end else...

		///---------------- Horizontal Edges -------------------------------------
		if (aboveMb != NULL)	///< Only look at macroblock boundary if there is a neighbour.
		{
			if (pMb->_intraFlag || aboveMb->_intraFlag)	///< Above intra macroblock boundary. (bS = 4)
			{
				for (j = 0; j < 16; j += 4)
					HorizontalFilter(pMb, lumRef, 1, 0, j, 4, 4);
				for (j = 0; j < 8; j += 4)
				{
					HorizontalFilter(pMb, cbRef, 0, 0, j, 4, 4);
					HorizontalFilter(pMb, crRef, 0, 0, j, 4, 4);
				}//end for j...
			}//end if _intraFlag...
			else																	///< Above inter macroblock boundary.
			{
				// TODO: For this current implementation only one 16x16 motion vector is used
				// per macroblock and from the same single reference.

				int mvDiffersBy4 = 0;	///< Differ with neighbour by 4 quarter pel values.
				if ((H264V2_FAST_ABS32(pMb->_mvX[0] - aboveMb->_mvX[0]) >= 4) || (H264V2_FAST_ABS32(pMb->_mvY[0] - aboveMb->_mvY[0]) >= 4))
					mvDiffersBy4 = 1;

				for (j = 0; j < 4; j++)
				{
					int bS = mvDiffersBy4;
					if (pMb->_lumBlk[0][j].GetNumCoeffs() || pMb->_lumBlk[0][j]._blkAbove->GetNumCoeffs())	///< Coded coeffs in block with q or block with p.
						bS = 2;

					if (bS)
					{
						/// Apply the filter to this macroblock block boundary.
						HorizontalFilter(pMb, lumRef, 1, 0, j << 2, 4, bS);	///< At (row = 0, col = 4*j) do iter = 4 cols.

						/// Apply to the aligned chr edge assuming 4:2:0 here only.
						HorizontalFilter(pMb, cbRef, 0, 0, j << 1, 2, bS);	///< At (row = 0, col = 2*j) do iter = 2 cols.
						HorizontalFilter(pMb, crRef, 0, 0, j << 1, 2, bS);	///< At (row = 0, col = 2*j) do iter = 2 cols.
					}//end if bS...
				}//end for j...

			}//end else...
		}//end aboveMb...

		if (pMb->_intraFlag)	///< Internal intra block edges.
		{
			for (i = 4; i < 16; i += 4)
				for (j = 0; j < 16; j += 4)
					HorizontalFilter(pMb, lumRef, 1, i, j, 4, 3);
			for (i = 4; i < 8; i += 4)
				for (j = 0; j < 8; j += 4)
				{
					HorizontalFilter(pMb, cbRef, 0, i, j, 4, 3);
					HorizontalFilter(pMb, crRef, 0, i, j, 4, 3);
				}//end for i & j...
		}//end if _intraFlag...
		else										///< Internal inter block edges.
		{
			// TODO: For this current implementation only one 16x16 motion vector is used
			// per macroblock and from the same single reference. Therefore all internal
			// blocks have the same motion vector i.e. difference = 0.

			for (i = 1; i < 4; i++)
				for (j = 0; j < 4; j++)
				{
					int bS = 0;
					if (pMb->_lumBlk[i][j].GetNumCoeffs() || pMb->_lumBlk[i][j]._blkAbove->GetNumCoeffs())	///< Coded coeffs in block with q or block with p.
						bS = 2;

					if (bS)
					{
						/// Apply the filter to this block boundary.
						HorizontalFilter(pMb, lumRef, 1, i << 2, j << 2, 4, bS);	///< At (row = 4*i, col = 4*j) do iter = 4 cols.

						/// Apply to the aligned chr edge assuming 4:2:0 here only.
						if (i == 2)
						{
							HorizontalFilter(pMb, cbRef, 0, i << 1, j << 1, 2, bS);	///< At (row = 2*i, col = 2*j) do iter = 2 cols.
							HorizontalFilter(pMb, crRef, 0, i << 1, j << 1, 2, bS);	///< At (row = 2*i, col = 2*j) do iter = 2 cols.
						}//end if i...
					}//end if bS...

				}//end for i & j...
		}//end else...

	}//end for mb...

}//end ApplyLoopFilter.

/** Apply the in-loop deblocking filter to vertical block edges.
The deblocking filter is applied only after the image has been fully
decoded. This method operates on one column with the given boundary
strength. The operation is defined in the ITU-T Recommendation
H.264 (03/2005).
@param pMb							: Macroblock to operate on.
@param img							: Reference image to filter.
@param lumFlag					: Indicates the colour component of the ref image.
@param rowOff						: The row offset within the macroblock with the top-left corner as (0,0).
@param colOff						: The column offset.
@param iter							: Iterations down the col from (rowOff, colOff).
@param boundaryStrength	: Boundary strength to apply.
@return									: none
*/
void H264v2Codec::VerticalFilter(MacroBlockH264* pMb, short** img, int lumFlag, int rowOff, int colOff, int iter, int boundaryStrength)
{
	int i;
	int qPav, offX, offY;
	if (lumFlag)
	{
		qPav = pMb->_mbQP;
		/// Modify to average qP with the neighbour if this is a mb edge.
		if ((pMb->_leftMb != NULL) && (colOff == 0))
			qPav = (qPav + pMb->_leftMb->_mbQP + 1) >> 1;
		offX = pMb->_offLumX + colOff;
		offY = pMb->_offLumY + rowOff;
	}//end if lumFlag...
	else
	{
		qPav = MacroBlockH264::GetQPc(pMb->_mbQP);
		if ((pMb->_leftMb != NULL) && (colOff == 0))
			qPav = (qPav + MacroBlockH264::GetQPc(pMb->_leftMb->_mbQP) + 1) >> 1;
		offX = pMb->_offChrX + colOff;
		offY = pMb->_offChrY + rowOff;
	}//end else...

	switch (boundaryStrength)
	{
	case 1:
	case 2:
	case 3:
	{
		int a = H264v2Codec::alpha[qPav];
		int b = H264v2Codec::beta[qPav];

		for (i = 0; i < iter; i++)	///< Iterate down the column.
		{
			int p1 = (int)(img[offY + i][offX - 2]);
			int p0 = (int)(img[offY + i][offX - 1]);
			int q0 = (int)(img[offY + i][offX]);
			int q1 = (int)(img[offY + i][offX + 1]);
			/// Test boundary differences to switch the filtering on/off.
			if ((H264V2_FAST_ABS32((p0 - q0)) < a) && (H264V2_FAST_ABS32((p1 - p0)) < b) && (H264V2_FAST_ABS32((q1 - q0)) < b))
			{
				int p2 = (int)(img[offY + i][offX - 3]);
				int q2 = (int)(img[offY + i][offX + 2]);
				int delta = (((q0 - p0) << 2) + (p1 - q1) + 4) >> 3;
				int tC0 = indexAbS[boundaryStrength - 1][qPav];
				int tC;
				if (lumFlag)
					tC = tC0 + ((H264V2_FAST_ABS32((p2 - p0)) < b) ? 1 : 0) + ((H264V2_FAST_ABS32((q2 - q0)) < b) ? 1 : 0);
				else
					tC = tC0 + 1;
				if (delta < -tC)	delta = -tC;
				else if (delta > tC)	delta = tC;

				img[offY + i][offX - 1] = (short)(H264V2_CLIP255(p0 + delta));			///< p0.
				img[offY + i][offX] = (short)(H264V2_CLIP255(q0 - delta));			///< q0.

				if ((H264V2_FAST_ABS32((p2 - p0)) < b) && lumFlag)
				{
					delta = (p2 + ((p0 + q0 + 1) >> 1) - (p1 << 1)) >> 1;
					if (delta < -tC0)	delta = -tC0;
					else if (delta > tC0)	delta = tC0;

					img[offY + i][offX - 2] = (short)(p1 + delta);		                ///< p1.
				}//end if abs(p2)...

				if ((H264V2_FAST_ABS32((q2 - q0)) < b) && lumFlag)
				{
					delta = (q2 + ((p0 + q0 + 1) >> 1) - (q1 << 1)) >> 1;
					if (delta < -tC0)	delta = -tC0;
					else if (delta > tC0)	delta = tC0;

					img[offY + i][offX + 1] = (short)(q1 + delta);		                ///< q1.
				}//end if abs(q2)...
			}//end if abs(p0)...
		}//end for i...
		break;
	}//end case 1,2 & 3...
	case 4:	///< This bS = 4 only occurs for intra boundaries and therefore must be colOff = 0;.
	{
		int a = H264v2Codec::alpha[qPav];
		int b = H264v2Codec::beta[qPav];

		for (i = 0; i < iter; i++)	///< Iterate down the column.
		{
			int p1 = (int)(img[offY + i][offX - 2]);
			int p0 = (int)(img[offY + i][offX - 1]);
			int q0 = (int)(img[offY + i][offX]);
			int q1 = (int)(img[offY + i][offX + 1]);
			/// Test boundary differences to switch the filtering on/off.
			if ((H264V2_FAST_ABS32((p0 - q0)) < a) && (H264V2_FAST_ABS32((p1 - p0)) < b) && (H264V2_FAST_ABS32((q1 - q0)) < b))
			{
				int p2 = (int)(img[offY + i][offX - 3]);
				int q2 = (int)(img[offY + i][offX + 2]);
				if ((H264V2_FAST_ABS32((p2 - p0)) < b) && (H264V2_FAST_ABS32((p0 - q0)) < ((a >> 2) + 2)) && lumFlag)
				{
					img[offY + i][offX - 1] = (short)((p2 + (2 * p1) + (2 * p0) + (2 * q0) + q1 + 4) >> 3);														///< p0.
					img[offY + i][offX - 2] = (short)((p2 + p1 + p0 + q0 + 2) >> 2);																						///< p1.
					img[offY + i][offX - 3] = (short)(((2 * (int)(img[offY + i][offX - 4])) + (3 * p2) + p1 + p0 + q0 + 4) >> 3);		///< p2.
				}//end if abs(p2)...
				else
				{
					img[offY + i][offX - 1] = (short)(((2 * p1) + p0 + q1 + 2) >> 2);																							///< p0.
				}//end else...

				if ((H264V2_FAST_ABS32((q2 - q0)) < b) && (H264V2_FAST_ABS32((p0 - q0)) < ((a >> 2) + 2)) && lumFlag)
				{
					img[offY + i][offX] = (short)((p1 + (2 * p0) + (2 * q0) + (2 * q1) + q2 + 4) >> 3);														///< q0.
					img[offY + i][offX + 1] = (short)((p0 + q0 + q1 + q2 + 2) >> 2);																						///< q1.
					img[offY + i][offX + 2] = (short)(((2 * (int)(img[offY + i][offX + 3])) + (3 * q2) + q1 + q0 + p0 + 4) >> 3);		///< q2.
				}//end if abs(p2)...
				else
				{
					img[offY + i][offX] = (short)(((2 * q1) + q0 + p1 + 2) >> 2);																									///< q0.
				}//end else...
			}//end if abs(p0)...
		}//end for i...

		break;
	}//end case 4...
	}//end switch boundaryStrength...

}//end VerticalFilter.

/** Apply the in-loop deblocking filter to horizontal block edges.
The deblocking filter is applied only after the image has been fully
decoded. This method operates on one row with the given boundary
strength. The operation is defined in the ITU-T Recommendation
H.264 (03/2005).
@param pMb							: Macroblock to operate on.
@param img							: Reference image to filter.
@param lumFlag					: Indicates the colour component of the ref image.
@param rowOff						: The row offset within the macroblock with the top-left corner as (0,0).
@param colOff						: The column offset.
@param iter							: Iterations along the row from (rowOff, colOff).
@param boundaryStrength	: Boundary strength to apply.
@return									: none
*/
void H264v2Codec::HorizontalFilter(MacroBlockH264* pMb, short** img, int lumFlag, int rowOff, int colOff, int iter, int boundaryStrength)
{
	int i;
	int qPav, offX, offY;
	if (lumFlag)
	{
		qPav = pMb->_mbQP;
		/// Modify to average qP with the neighbour if this is a mb edge.
		if ((pMb->_aboveMb != NULL) && (rowOff == 0))
			qPav = (qPav + pMb->_aboveMb->_mbQP + 1) >> 1;
		offX = pMb->_offLumX + colOff;
		offY = pMb->_offLumY + rowOff;
	}//end if lumFlag...
	else
	{
		qPav = MacroBlockH264::GetQPc(pMb->_mbQP);
		if ((pMb->_aboveMb != NULL) && (rowOff == 0))
			qPav = (qPav + MacroBlockH264::GetQPc(pMb->_aboveMb->_mbQP) + 1) >> 1;
		offX = pMb->_offChrX + colOff;
		offY = pMb->_offChrY + rowOff;
	}//end else...

	switch (boundaryStrength)
	{
	case 1:
	case 2:
	case 3:
	{
		int a = H264v2Codec::alpha[qPav];
		int b = H264v2Codec::beta[qPav];

		for (i = 0; i < iter; i++)	///< Iterate along the row.
		{
			int p1 = (int)(img[offY - 2][offX + i]);
			int p0 = (int)(img[offY - 1][offX + i]);
			int q0 = (int)(img[offY][offX + i]);
			int q1 = (int)(img[offY + 1][offX + i]);
			/// Test boundary differences to switch the filtering on/off.
			if ((H264V2_FAST_ABS32((p0 - q0)) < a) && (H264V2_FAST_ABS32((p1 - p0)) < b) && (H264V2_FAST_ABS32((q1 - q0)) < b))
			{
				int p2 = (int)(img[offY - 3][offX + i]);
				int q2 = (int)(img[offY + 2][offX + i]);
				int delta = (((q0 - p0) << 2) + (p1 - q1) + 4) >> 3;
				int tC0 = indexAbS[boundaryStrength - 1][qPav];
				int tC;
				if (lumFlag)
					tC = tC0 + ((H264V2_FAST_ABS32((p2 - p0)) < b) ? 1 : 0) + ((H264V2_FAST_ABS32((q2 - q0)) < b) ? 1 : 0);
				else
					tC = tC0 + 1;
				if (delta < -tC)	delta = -tC;
				else if (delta > tC)	delta = tC;

				img[offY - 1][offX + i] = (short)(H264V2_CLIP255(p0 + delta));			///< p0.
				img[offY][offX + i] = (short)(H264V2_CLIP255(q0 - delta));			///< q0.

				if ((H264V2_FAST_ABS32((p2 - p0)) < b) && lumFlag)
				{
					delta = (p2 + ((p0 + q0 + 1) >> 1) - (p1 << 1)) >> 1;
					if (delta < -tC0)	delta = -tC0;
					else if (delta > tC0)	delta = tC0;

					img[offY - 2][offX + i] = (short)(p1 + delta);			///< p1.
				}//end if abs(p2)...

				if ((H264V2_FAST_ABS32((q2 - q0)) < b) && lumFlag)
				{
					delta = (q2 + ((p0 + q0 + 1) >> 1) - (q1 << 1)) >> 1;
					if (delta < -tC0)	delta = -tC0;
					else if (delta > tC0)	delta = tC0;

					img[offY + 1][offX + i] = (short)(q1 + delta);			///< q1.
				}//end if abs(q2)...
			}//end if abs(p0)...
		}//end for i...
		break;
	}//end case 1,2 & 3...
	case 4:	///< This bS = 4 only occurs for intra boundaries and therefore must be rowOff = 0;.
	{
		int a = H264v2Codec::alpha[qPav];
		int b = H264v2Codec::beta[qPav];

		for (i = 0; i < iter; i++)	///< Iterate along the row.
		{
			int p1 = (int)(img[offY - 2][offX + i]);
			int p0 = (int)(img[offY - 1][offX + i]);
			int q0 = (int)(img[offY][offX + i]);
			int q1 = (int)(img[offY + 1][offX + i]);
			/// Test boundary differences to switch the filtering on/off.
			if ((H264V2_FAST_ABS32((p0 - q0)) < a) && (H264V2_FAST_ABS32((p1 - p0)) < b) && (H264V2_FAST_ABS32((q1 - q0)) < b))
			{
				int p2 = (int)(img[offY - 3][offX + i]);
				int q2 = (int)(img[offY + 2][offX + i]);
				if ((H264V2_FAST_ABS32((p2 - p0)) < b) && (H264V2_FAST_ABS32((p0 - q0)) < ((a >> 2) + 2)) && lumFlag)
				{
					img[offY - 1][offX + i] = (short)((p2 + (2 * p1) + (2 * p0) + (2 * q0) + q1 + 4) >> 3);														///< p0.
					img[offY - 2][offX + i] = (short)((p2 + p1 + p0 + q0 + 2) >> 2);																						///< p1.
					img[offY - 3][offX + i] = (short)(((2 * (int)(img[offY - 4][offX + i])) + (3 * p2) + p1 + p0 + q0 + 4) >> 3);		///< p2.
				}//end if abs(p2)...
				else
				{
					img[offY - 1][offX + i] = (short)(((2 * p1) + p0 + q1 + 2) >> 2);																							///< p0.
				}//end else...

				if ((H264V2_FAST_ABS32((q2 - q0)) < b) && (H264V2_FAST_ABS32((p0 - q0)) < ((a >> 2) + 2)) && lumFlag)
				{
					img[offY][offX + i] = (short)((p1 + (2 * p0) + (2 * q0) + (2 * q1) + q2 + 4) >> 3);														///< q0.
					img[offY + 1][offX + i] = (short)((p0 + q0 + q1 + q2 + 2) >> 2);																						///< q1.
					img[offY + 2][offX + i] = (short)(((2 * (int)(img[offY + 3][offX + i])) + (3 * q2) + q1 + q0 + p0 + 4) >> 3);		///< q2.
				}//end if abs(p2)...
				else
				{
					img[offY][offX + i] = (short)(((2 * q1) + q0 + p1 + 2) >> 2);																									///< q0.
				}//end else...
			}//end if abs(p0)...
		}//end for i...

		break;
	}//end case 4...
	}//end switch boundaryStrength...

}//end HorizontalFilter.

/** Transform and Quantise an Intra_16x16 macroblock
This method provides a speed improvement for macroblock processing and code refactoring.
@param pMb	: Macroblock to transform.
@return			: none
*/
void H264v2Codec::TransAndQuantIntra16x16MBlk(MacroBlockH264* pMb)
{
	int i;
	int mbLumQP = pMb->_mbQP;
	int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);

	/// The transform (mode = TransformOnly) and quantisation (mode = QuantOnly) are separated 
	/// for the 4x4 AC blocks in this method and therefore the quant parameter does not need to 
	/// be set. But the DC blocks are in the TransformAndQuant mode and require the setting.
	_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, mbLumQP);
	_pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_pFDC4x4T->SetParameter(IForwardTransform::QUANT_ID, mbLumQP);
	_pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);

	/// Do the forward 4x4 transform on the non-DC 4x4 blocks without scaling or quantisation. Pull
	/// out the DC terms from each block and populate the DC blocks and then scale and quant the
	/// non-DC 4x4 blocks. Note that this is done in raster scan order and not coding order to
	/// align each position in the DC block with the spatial location of the 4x4 block in the image.
	BlockH264*	pCbBlk = &(pMb->_cbBlk[0][0]);	///< Assume these are linear arrays that wrap in raster scan order.
	BlockH264*	pCrBlk = &(pMb->_crBlk[0][0]);
	short*			pDcCbBlk = pMb->_cbDcBlk.GetBlk();
	short*			pDcCrBlk = pMb->_crDcBlk.GetBlk();
	for (i = 0; i < 4; i++)
	{
		/// Unroll the inner loop.

		short*			pDcLumBlk = &((pMb->_lumDcBlk.GetBlk())[4 * i]);	///< Short cuts.
		BlockH264*	pLumBlk = &(pMb->_lumBlk[i][0]);

		/// Transform without scaling or quant.
		_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);

		pLumBlk->ForwardTransform(_pF4x4TLum);	///< [i][0]
		*pDcLumBlk++ = pLumBlk->GetDC(); ///< Set the Lum DC block from the DC term of this block and scale it without rounding.
		(pLumBlk++)->SetDC(0); ///< Required to ensure the feedback loop operates properly.

		pLumBlk->ForwardTransform(_pF4x4TLum);	///< [i][1]
		*pDcLumBlk++ = pLumBlk->GetDC();
		(pLumBlk++)->SetDC(0);

		pLumBlk->ForwardTransform(_pF4x4TLum);	///< [i][2]
		*pDcLumBlk++ = pLumBlk->GetDC();
		(pLumBlk++)->SetDC(0);

		pLumBlk->ForwardTransform(_pF4x4TLum);	///< [i][3]
		*pDcLumBlk = pLumBlk->GetDC();
		pLumBlk->SetDC(0);

		_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);

		pCbBlk->ForwardTransform(_pF4x4TChr);	///< [i]
		*pDcCbBlk++ = pCbBlk->GetDC();
		pCbBlk->SetDC(0);

		pCrBlk->ForwardTransform(_pF4x4TChr);	///< [i]
		*pDcCrBlk++ = pCrBlk->GetDC();
		pCrBlk->SetDC(0);

		/// Now scale and quant.
		_pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
		(pLumBlk--)->Quantise(_pF4x4TLum);	///< [i][3]
		(pLumBlk--)->Quantise(_pF4x4TLum);	///< [i][2]
		(pLumBlk--)->Quantise(_pF4x4TLum);	///< [i][1]
		pLumBlk->Quantise(_pF4x4TLum);			///< [i][0]
		_pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
		(pCbBlk++)->Quantise(_pF4x4TChr);
		(pCrBlk++)->Quantise(_pF4x4TChr);

	}//end for i...

	/// Transform and quant the DC blocks.
	pMb->_lumDcBlk.ForwardTransform(_pFDC4x4T);
	pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
	pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

}//end TransAndQuantIntra16x16MBlk.


/** Transform and Quantise an Intra_16x16 macroblock to below a max distortion criterion.
This overload method decrements the mb QP value from its existing value until the mb distortion
drops below the specified Dmax value. It returns the distortion at the final value of QP that
meets the criterion and places the decoded mb values in the temp blks. The final inverse decoded
blks are stored in the tmp blks.
@param pMb	  : Macroblock to transform.
@param Dmax   : Target max distortion criterion.
@param minQP  : Lowest QP value allowed.
@return			  : Lum distortion.
*/
int H264v2Codec::TransAndQuantIntra16x16MBlk(MacroBlockH264* pMb, int Dmax, int minQP)
{
	int i, j;
	int qp = pMb->_mbQP;
	int distortion = 0;

	///------------ Lum in raster scan order--------------------------

	/// Transform without quant to extract the DC term. This is then used as the starting storage
	/// point to reload on each iteration of the Dmax descent. The main blks are only quantised 
	/// when the final QP value is determined.
	_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	short* pDcLumBlk = pMb->_lumDcBlk.GetBlk();  ///< Short cut to DC blk.
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			BlockH264* pb = &(pMb->_lumBlk[i][j]);

			/// Make a copy of the input lum blks for later distortion calculations. These input
		  /// blks enter this method holding the diff between the input and the pred images.
			pb->Copy(_tmpBlk[i][j].GetBlk());
			/// DCT in-place but no quant.
			pb->ForwardTransform(_pF4x4TLum);   ///< DCT.
			*pDcLumBlk++ = pb->GetDC();         ///< Set DC blk
			pb->SetDC(0);                       ///< Clear DC term in AC blk.
		}//end for i & j...

		/// Transform the DC blocks.
	_pFDC4x4T->SetMode(IForwardTransform::TransformOnly);
	pMb->_lumDcBlk.ForwardTransform(_pFDC4x4T);

	/// Iterate QP down from qp to either distortion <= Dmax or minQP (without distortion <= Dmax is best 
	/// that can be done).
  _pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
  _pFDC4x4T->SetMode(IForwardTransform::QuantOnly);

  /// Two options:
  ///  1. QP descent algorithm in fixed step sizes.
  ///  2. Interval halving/segmenting algorithm.
/*
  do  ///< Option 1
	{

    /// Code refactoring to Copy into temp blocks, quantise, inverse
    /// quantise and DCT and determine distortion or ROI distortion.
    distortion = IDR_Copy_Q_IQ_IT(pMb, qp);

		/// Update mb QP.
		pMb->_mbQP = qp;

		/// New qp.
		qp -= 4; //MbStepSize[qp];
		if (qp < minQP) qp = minQP;

	} while ((qp > minQP) && (distortion > Dmax)); //end do while qp...
*/

  /// Option 2
  /// An interval halving algorithm with initial guess of neighbourhood QP prediction
  /// and limiting the change in QP range.
  int low = minQP;
  int high = qp;
  int fivePercent = ((Dmax * 5) + 50) / 100;

  /// Impose a limit on delta QP from previous mb QP. 
  int prevQP = GetPrevMbEncQP(pMb);
  qp = SetQPRangeLimits(qp, prevQP, 13, minQP, H264V2_MAX_QP, &high, &low);

  bool close = false;
  if (_modeOfOperation == H264V2_MINAVG_RATECNT)
  {
    /// The davg value is already set as that required for the remaining mbs from this point. If this
    /// mb is the 1st mb in a row then re-calibrate the QP to match davg. For all other mbs, predict a
    /// QP based on previous mbs to target davg.
    int davg = Dmax;  ///< davg is in units of square error per mb.
    qp = prevQP;      ///< Best starting QP is from the previous mb. 

    /// The 1st mb in a row can be found from the left neighbourhood pointer equal to NULL.
    if (pMb->_leftMb == NULL)
    {
      /// Re-calibrate QP to davg.
      do  ///< Until high-low gap is small or within 5% of davg
      {
        /// Forward quant and inverse quant and transform. Accumulate ROI distortion.
        distortion = IDR_Copy_Q_IQ_IT(pMb, qp); ///< Code re-factoring method
        /// Update mb QP with the last values used in the while loop
        pMb->_mbQP = pMb->_mbEncQP = qp;

        /// Set the new interval boundaries
        if (davg >= distortion) low = qp;
        else high = qp;
        /// New qp.
        qp = (low + high) / 2;  ///< Halve the interval.

        close = abs(davg - distortion) < fivePercent;

      } while (((high - low) > 3) && !close); //end do while high...
    }//end if !_leftMb...
    else  ///< else all other row mbs...
    {
      /// Predict next QP for desired davg.
      qp = GetPredQPStepMethod(pMb, davg, fivePercent, low, high);
      /// Forward quant and inverse quant and transform. Accumulate ROI distortion.
      distortion = IDR_Copy_Q_IQ_IT(pMb, qp); ///< Code re-factoring method
      /// Update mb QP with the last values used.
      pMb->_mbQP = pMb->_mbEncQP = qp;
    }//end else...

  }//end if H264V2_MINAVG_RATECNT...
  else ///< if(_modeOfOperation == H264V2_MINMAX_RATECNT)
  {
    do
    {
      /// Forward quant and inverse quant and transform. Accumulate ROI distortion.
      distortion = IDR_Copy_Q_IQ_IT(pMb, qp); ///< Code re-factoring method
      /// Update mb QP with the last values used in the while loop
      pMb->_mbQP = pMb->_mbEncQP = qp;

      /// Set the new interval boundaries
      if (Dmax >= distortion) low = qp;
      else high = qp;
      /// New qp.
      qp = (low + high) / 2;  ///< Halve the interval.

      int diff = (Dmax - distortion);
      close = (diff > 0) && (diff < fivePercent);

    } while (((high - low) > 4) && !close); //end do while high...
//  } while (((high - low) > 1) && !close); //end do while high...

    /// Check to make sure the selected QP does result in a lower distortion than Dmax. Otherwise
    /// use the last low boundary qp value.
    if (Dmax < distortion)
    {
      distortion = IDR_Copy_Q_IQ_IT(pMb, low);
      /// Update mb QP with the last values used in the while loop
      pMb->_mbQP = pMb->_mbEncQP = low;
    }//end if Dmax...
  }//end else...

  /// Finally quant the main lum AC and DC blks.
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			pMb->_lumBlk[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
		}//end for i & j...
	pMb->_lumDcBlk.ForwardTransform(_pFDC4x4T);

	///----------- Chr in raster scan order --------------------------

	int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);
	/// Forward.
	_pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	_pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	/// Inverse.
	_pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
	_pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

	short* pDcCbBlk = pMb->_cbDcBlk.GetBlk();
	short* pDcCrBlk = pMb->_crDcBlk.GetBlk();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
		{
			BlockH264*  pCb = &(pMb->_cbBlk[i][j]);
			BlockH264*  pCr = &(pMb->_crBlk[i][j]);

			_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
			/// Cb
			pCb->ForwardTransform(_pF4x4TChr);  ///< DCT.
			*pDcCbBlk++ = pCb->GetDC();         ///< Set DC blk
			pCb->SetDC(0);                      ///< Clear DC term in AC blk.

			/// Cr
			pCr->ForwardTransform(_pF4x4TChr);  ///< DCT.
			*pDcCrBlk++ = pCr->GetDC();         ///< Set DC blk
			pCr->SetDC(0);                      ///< Clear DC term in AC blk.

			_pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
			pCb->Quantise(_pF4x4TChr);         ///< Scale and quant.
			pCr->Quantise(_pF4x4TChr);
		}//end for i & j...

		/// Transform and quant the DC blocks.
	pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
	pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

	/// Copy the Chr blks into the temp blks for inverse processing.
	MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_CB_DC, MBH264_CR_1_1);

	/// Inverse quant and inverse transform the DC blks.
	pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
	pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

	/// Inverse quant then add DC term before inverse transform.
	short* pDcCbBlkTmp = pMb->_cbDcBlkTmp.GetBlk();
	short* pDcCrBlkTmp = pMb->_crDcBlkTmp.GetBlk();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
		{
			BlockH264*  pCbT = &(pMb->_cbBlkTmp[i][j]);
			BlockH264*  pCrT = &(pMb->_crBlkTmp[i][j]);

			_pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
			pCbT->InverseQuantise(_pI4x4TChr);  ///< Inverse scale and quant.
			pCbT->SetDC(*pDcCbBlkTmp++);        ///< Load the DC term from the DC blk.
			pCrT->InverseQuantise(_pI4x4TChr);
			pCrT->SetDC(*pDcCrBlkTmp++);

			_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
			pCbT->InverseTransform(_pI4x4TChr);  ///< Inverse transform.
			pCrT->InverseTransform(_pI4x4TChr);
		}//end for i & j...

	  /// Restore state.
	_pFDC4x4T->SetMode(IForwardTransform::TransformAndQuant);
	pMb->_mbEncQP = pMb->_mbQP;

	return(distortion);
}//end TransAndQuantIntra16x16MBlk.

/** Code Refactoring: Used in the TransAndQuantIntra16x16MBlk() method
Accumulate mb distortion from copying blks to tmp blks, Quant, Inverse Quant and
Inverse transform of the _lumBlkTmp[][] including the DC block.
@param pMb    : Macroblock to operate on
@param eqp    : QP
@return       : distortion.
*/
int H264v2Codec::IDR_Copy_Q_IQ_IT(MacroBlockH264* pMb, int qp)
{
  int i, j;
  int distortion = 0;

  /// Forward.
  _pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
  _pFDC4x4T->SetParameter(IForwardTransform::QUANT_ID, qp);
  /// Inverse
  _pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, qp);
  _pIDC4x4T->SetParameter(IInverseTransform::QUANT_ID, qp);

  /// Copy the Lum blks into the temp Lum blks for forward quant and inverse transform + quant processing.
  MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_DC, MBH264_LUM_3_3);

  /// Quant the temp Lum blks.
  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
    {
      pMb->_lumBlkTmp[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
    }//end for i & j...

     /// Quant the temp DC blocks.
  pMb->_lumDcBlkTmp.ForwardTransform(_pFDC4x4T);

  /// For QP = H264V2_MAX_QP it is likely that all coeffs are zeros. Find the QP value 
  /// that results in at least 1 non-zero coeff.
  //    if(qp == H264V2_MAX_QP)
  //      qp = GetQPtoNonZeroCoeff(pMb, minQP);

  /// Inverse quant and inverse transform the DC blks.
  pMb->_lumDcBlkTmp.InverseTransform(_pIDC4x4T);

  /// Inverse quant then add DC term before inverse transform. Accumulate distortion.
  distortion = 0;
  short* pDcLumBlkTmp = pMb->_lumDcBlkTmp.GetBlk();  ///< Short cut to DC blk.
  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
    {
      BlockH264* pb = &(pMb->_lumBlkTmp[i][j]);

      _pI4x4TLum->SetMode(IInverseTransform::QuantOnly);
      pb->InverseQuantise(_pI4x4TLum);	///< Inverse scale and quant.
      pb->SetDC(*pDcLumBlkTmp++);       ///< Load the DC term from the DC blk.

      _pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
      pb->InverseTransform(_pI4x4TLum); ///< Inverse transform.

                                        /// Total square diff with the input store.
#ifdef USE_ABSOLUTE_DIFFERENCE
      distortion += pb->GetBlkOverlay()->Tad4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#else
      distortion += pb->GetBlkOverlay()->Tsd4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#endif
    }//end for i & j...

  /// Region of interest multiplier application to the whole mb.
  distortion = ROIDistortion(pMb->_mbIndex, distortion);

  return(distortion);
}//end IDR_Copy_Q_IQ_IT. 

/** Code Refactoring: Used in the TransAndQuantIntra16x16MBlk() and TransAndQuantInter16x16MBlk()method
Predict a QP value from the neighbouring mbs to achieve a target distortion.
@param pMb               : Macroblock to operate on
@param targetDistortion  : The distortion required
@param tolerance         : Distortion tolerance above and below targetDistortion for no change in QP
@param low               : Lower limit on QP
@param high              : Upper limit on QP
@return                  : New QP.
*/
int H264v2Codec::GetPredQPStepMethod(MacroBlockH264* pMb, int targetDistortion, int tolerance, int low, int high)
{
  /// To align with the closest previous mb QP, use the left mb if it exists, otherwise use the above mb. 
  int qp = _slice._qp;  ///< (Defualt)
  if (pMb->_mbIndex)
  {
    /// The mb reference will always be valid because only the top left has neither and it is
    /// excluded by the if _mbIndex decision above.
    MacroBlockH264* prevMb = pMb->_leftMb;
    if (prevMb == NULL)  prevMb = pMb->_aboveMb;
    qp = prevMb->_mbEncQP;

    int prevDistortion = prevMb->_distortion[qp];

    if (prevDistortion > (targetDistortion + tolerance)) qp--;
    if (prevDistortion > (targetDistortion + (2*tolerance))) qp--;
    if (prevDistortion < (targetDistortion - tolerance)) qp++;
    if (prevDistortion < (targetDistortion - (2*tolerance))) qp++;

    if (qp < low)  qp = low;
    if (qp > high) qp = high;

  }//end if _mbIndex...
  return(qp);
}//end GetPredQPStepMethod.

/** Code refactoring for TransAndQuantIntra16x16MBlk() method.
Get the mb QP value such that there is at least one non-zero DCT coeff.
@param pMb	  : Macroblock to transform.
@return       : QP value for non-zero coeffs.
*/
int H264v2Codec::GetQPtoNonZeroCoeff(MacroBlockH264* pMb, int minQP)
{
	int i, j;
	int qp = H264V2_MAX_QP;

	/// Are all coeffs zeros?
	int isZero = pMb->_lumDcBlkTmp.IsZero2();
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			isZero &= pMb->_lumBlkTmp[i][j].IsZero2();
	if (isZero)
	{
		/// Re-quant at QP = 1.
		_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, 1);
		_pFDC4x4T->SetParameter(IForwardTransform::QUANT_ID, 1);
		MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_DC, MBH264_LUM_3_3);
		pMb->_lumDcBlkTmp.ForwardTransform(_pFDC4x4T);
		for (i = 0; i < 4; i++)
			for (j = 0; j < 4; j++)
				pMb->_lumBlkTmp[i][j].Quantise(_pF4x4TLum);

		/// Find the blk and position of the peak peak value.
		BlockH264*  pPeakBlk = &(pMb->_lumDcBlkTmp);
		short*      pB = pPeakBlk->GetBlk();
		int         peakPos = 0;
		short       peakVal = abs(pB[0]);

		for (i = 1; i < 16; i++) ///< DC blk.
		{
			if (abs(pB[i]) > peakVal)
			{
				peakPos = i;
				peakVal = abs(pB[i]);
			}//end if pB[]...
		}//end for i...

		for (i = 0; i < 4; i++)  /// AC blks.
			for (j = 0; j < 4; j++)
			{
				pB = pMb->_lumBlkTmp[i][j].GetBlk();
				for (int k = 0; k < 16; k++)
				{
					if (abs(pB[k]) > peakVal)
					{
						peakPos = k;
						peakVal = abs(pB[k]);
						pPeakBlk = &(pMb->_lumBlkTmp[i][j]);
					}//end if abs(pB[])...
				}//end for k...
			}//end for i & j...

		  /// Get the pre-quant value from the main blks in the position required.
		short val = (pPeakBlk->GetBlk())[peakPos];
		IForwardTransform* pQuantiser = _pF4x4TLum;
		if (pPeakBlk->IsDc())
			pQuantiser = _pFDC4x4T;

		/// Quantise this single peak value until it is non-zero.
		qp = H264V2_MAX_QP - 1;
		while ((qp > minQP) && (pQuantiser->QuantiseValue(val, peakPos, qp) == 0))
		{
			qp -= MbStepSize[qp];
			if (qp < minQP) qp = minQP;
		}//end while qp...

		/// Re-load and quantise the whole mb at this new QP value.
		_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
		_pFDC4x4T->SetParameter(IForwardTransform::QUANT_ID, qp);
		MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_DC, MBH264_LUM_3_3);
		/// Quant the temp Lum blks.
		_pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
		for (i = 0; i < 4; i++)
			for (j = 0; j < 4; j++)
				pMb->_lumBlkTmp[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
			  /// Quant the temp DC blocks.
		_pFDC4x4T->SetMode(IForwardTransform::QuantOnly);
		pMb->_lumDcBlkTmp.ForwardTransform(_pFDC4x4T);

	}//end if isZero...

	return(qp);
}//end SetQPtoNonZeroCoeff.

/** Transform and Quantise an Intra_16x16 macroblock with Inverse.
This method provides a speed improvement for macroblock processing and code refactoring. The
inverse transform and distortion measurment is included to the temp blks.
@param pMb	  : Macroblock to transform.
@param withD  : Include distortion calculation.
@return			  : distortion.
*/
int H264v2Codec::TransAndQuantIntra16x16MBlkWithInv(MacroBlockH264* pMb, int withD)
{
	int i, j;
	int mbLumQP = pMb->_mbQP;
	int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);
	int distortion = 0;

	/// The transform (mode = TransformOnly) and quantisation (mode = QuantOnly) are separated 
	/// for the 4x4 AC blocks in this method. The DC blocks are in the TransformAndQuant mode.
	_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, mbLumQP);
	_pFDC4x4T->SetParameter(IForwardTransform::QUANT_ID, mbLumQP);
	_pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, mbLumQP);
	_pIDC4x4T->SetParameter(IInverseTransform::QUANT_ID, mbLumQP);

	_pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	_pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	_pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
	_pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

	/// Do the forward 4x4 transform on the non-DC 4x4 blocks without scaling or quantisation. Pull
	/// out the DC terms from each block and populate the DC blocks and then scale and quant the
	/// non-DC 4x4 blocks. Note that this is done in raster scan order and not coding order to
	/// align each position in the DC block with the spatial location of the 4x4 block in the image.
  /// Copy the coeffs to the temp blks and do the inverse process.

  ///------------ Lum in raster scan order--------------------------

	short* pDcLumBlk = pMb->_lumDcBlk.GetBlk();  ///< Short cut to DC blk.
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			/// Make a copy of the input lum blks for later distortion calculations. These input
		  /// blks enter this method holding the diff between the input and the pred images.
			if (withD)
				pMb->_lumBlk[i][j].Copy(_tmpBlk[i][j].GetBlk());

			_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
			pMb->_lumBlk[i][j].ForwardTransform(_pF4x4TLum);  ///< DCT.
			pDcLumBlk[i * 4 + j] = pMb->_lumBlk[i][j].GetDC();  ///< Set DC blk
			pMb->_lumBlk[i][j].SetDC(0);                      ///< Clear DC term in AC blk.

			_pF4x4TLum->SetMode(IForwardTransform::QuantOnly); ///< Scale and quant.
			pMb->_lumBlk[i][j].Quantise(_pF4x4TLum);

		}//end for i & j...

	// Transform and quant the DC blocks.
	pMb->_lumDcBlk.ForwardTransform(_pFDC4x4T);

	/// Copy the Lum blks into the temp Lum blks for inverse transform + quant processing.
	MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_DC, MBH264_LUM_3_3);

	/// Inverse quant and inverse transform the temp DC blks.
	pMb->_lumDcBlkTmp.InverseTransform(_pIDC4x4T);

	/// Inverse quant then add DC term before inverse transform. Accumulate distortion.
	short* pDcLumBlkTmp = pMb->_lumDcBlkTmp.GetBlk();  ///< Short cut to temp DC blk.
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			_pI4x4TLum->SetMode(IInverseTransform::QuantOnly);
			pMb->_lumBlkTmp[i][j].InverseQuantise(_pI4x4TLum);	///< Inverse scale and quant.
			pMb->_lumBlkTmp[i][j].SetDC(pDcLumBlkTmp[4 * i + j]); ///< Load the DC term from the DC blk.

			_pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
			pMb->_lumBlkTmp[i][j].InverseTransform(_pI4x4TLum); ///< Inverse transform.

			/// Total square diff with the input store.
			if (withD)
#ifdef USE_ABSOLUTE_DIFFERENCE
				distortion += pMb->_lumBlkTmp[i][j].GetBlkOverlay()->Tad4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#else
				distortion += pMb->_lumBlkTmp[i][j].GetBlkOverlay()->Tsd4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#endif
		}//end for i & j...

  /// Region of interest multiplier application to the whole mb.
  distortion = ROIDistortion(pMb->_mbIndex, distortion);

  ///----------- Chr in raster scan order --------------------------

	short* pDcCbBlk = pMb->_cbDcBlk.GetBlk();
	short* pDcCrBlk = pMb->_crDcBlk.GetBlk();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
		{
			_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
			/// Cb
			pMb->_cbBlk[i][j].ForwardTransform(_pF4x4TChr); ///< DCT.
			pDcCbBlk[i * 2 + j] = pMb->_cbBlk[i][j].GetDC();  ///< Set DC blk
			pMb->_cbBlk[i][j].SetDC(0);                     ///< Clear DC term in AC blk.

			/// Cr
			pMb->_crBlk[i][j].ForwardTransform(_pF4x4TChr); ///< DCT.
			pDcCrBlk[i * 2 + j] = pMb->_crBlk[i][j].GetDC();  ///< Set DC blk
			pMb->_crBlk[i][j].SetDC(0);                     ///< Clear DC term in AC blk.

			_pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
			pMb->_cbBlk[i][j].Quantise(_pF4x4TChr);         ///< Scale and quant.
			pMb->_crBlk[i][j].Quantise(_pF4x4TChr);
		}//end for i & j...

		/// Transform and quant the DC blocks.
	pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
	pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

	/// Copy the Chr blks into the temp blks for inverse processing.
	MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_CB_DC, MBH264_CR_1_1);

	/// Inverse quant and inverse transform the DC blks.
	pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
	pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

	/// Inverse quant then add DC term before inverse transform.
	short* pDcCbBlkTmp = pMb->_cbDcBlkTmp.GetBlk();
	short* pDcCrBlkTmp = pMb->_crDcBlkTmp.GetBlk();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
		{
			_pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
			pMb->_cbBlkTmp[i][j].InverseQuantise(_pI4x4TChr); ///< Inverse scale and quant.
			pMb->_cbBlkTmp[i][j].SetDC(pDcCbBlkTmp[2 * i + j]); ///< Load the DC term from the DC blk.
			pMb->_crBlkTmp[i][j].InverseQuantise(_pI4x4TChr);
			pMb->_crBlkTmp[i][j].SetDC(pDcCrBlkTmp[2 * i + j]);

			_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
			pMb->_cbBlkTmp[i][j].InverseTransform(_pI4x4TChr);  ///< Inverse transform.
			pMb->_crBlkTmp[i][j].InverseTransform(_pI4x4TChr);
		}//end for i & j...

	return(distortion);
}//end TransAndQuantIntra16x16MBlkWithInv.

/** Transform and Quantise an Intra_16x16 mode block.
Code refactoring method to transform the block and store the DC coeff before quantisation. The
transform must have the quantisation parameter preset before calling this method.
@param pTQ					: Transform to use.
@param pBlk					: Blk to process.
@param pDcBlkCoeff	: reference to store the DC coeff.
@return							: none.
*/
void H264v2Codec::TransAndQuantIntra16x16ModeBlk(IForwardTransform* pTQ, BlockH264* pBlk, short* pDcBlkCoeff)
{
	pTQ->SetMode(IForwardTransform::TransformOnly);
	pBlk->ForwardTransform(pTQ);
	*pDcBlkCoeff = pBlk->GetDC();	///< Set the DC block from the DC term of this block.
	pBlk->SetDC(0);								///< Required to ensure the feedback loop operates properly.
	pTQ->SetMode(IForwardTransform::QuantOnly);
	pBlk->Quantise(pTQ);
}//end TransAndQuantIntra16x16ModeBlk.

/** Inverse Transform and Quantise an Intra_16x16 macroblock
This method provides a speed improvement for macroblock processing and code refactoring.
@param pMb				: Macroblock to transform.
@param tmpBlkFlag	: Indicate temp blocks to be used.
@return						: none
*/
void H264v2Codec::InverseTransAndQuantIntra16x16MBlk(MacroBlockH264* pMb, int tmpBlkFlag)
{
	int					i;
	BlockH264*	pCbBlk;
	BlockH264*	pCrBlk;
	short*			pDcCbBlk;
	short*			pDcCrBlk;
	int					mbLumQP = pMb->_mbQP;
	int					mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);

	/// The inverse transform (mode = TransformOnly) and quantisation (mode = QuantOnly) are separated 
	/// for the 4x4 AC blocks in this method and therefore the quant parameter does not need to 
	/// be set. But the DC blocks are in the TransformAndQuant mode and require the setting.
	_pIDC4x4T->SetParameter(IInverseTransform::QUANT_ID, mbLumQP);
	_pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

	if (tmpBlkFlag)
	{
		/// Copy all blks to temp blks.
		MacroBlockH264::CopyBlksToTmpBlks(pMb, 0, MBH264_NUM_BLKS - 1);

		/// Inverse transform & inverse quantise DC blocks.
		pMb->_lumDcBlkTmp.InverseTransform(_pIDC4x4T);
		pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
		pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

		/// Short cut pointers for Chr components.
		pCbBlk = &(pMb->_cbBlkTmp[0][0]);	///< Assume these are linear arrays that wrap in raster scan order.
		pCrBlk = &(pMb->_crBlkTmp[0][0]);
		pDcCbBlk = pMb->_cbDcBlkTmp.GetBlk();	///< Actual array of coeffs.
		pDcCrBlk = pMb->_crDcBlkTmp.GetBlk();
	}//end if tmpBlkFlag...
	else
	{
		/// Inverse transform & inverse quantise DC blocks.
		pMb->_lumDcBlk.InverseTransform(_pIDC4x4T);
		pMb->_cbDcBlk.InverseTransform(_pIDC2x2T);
		pMb->_crDcBlk.InverseTransform(_pIDC2x2T);

		/// Short cut pointers for Chr components.
		pCbBlk = &(pMb->_cbBlk[0][0]);	///< Assume these are linear arrays that wrap in raster scan order.
		pCrBlk = &(pMb->_crBlk[0][0]);
		pDcCbBlk = pMb->_cbDcBlk.GetBlk();	///< Actual array of coeffs.
		pDcCrBlk = pMb->_crDcBlk.GetBlk();
	}//end else...

	/// Inverse quant for the main Lum, Cb and Cr blocks, insert the DC terms from the 
	/// DC blocks and then complete the inverse transform process. Note that this is 
	/// done in raster scan order and not coding order to align each position in the 
	/// DC block with the spatial location of the 4x4 block in the image.

	for (i = 0; i < 4; i++)	///< 16 Lum blocks + 4 Cb Chr and 4 Cr Chr blocks.
	{
		short*			pDcLumBlk;
		BlockH264*	pLumBlk;

		/// Unroll the inner loop.

		if (tmpBlkFlag)
		{
			pDcLumBlk = &((pMb->_lumDcBlkTmp.GetBlk())[4 * i + 3]);	///< Short cuts.
			pLumBlk = &(pMb->_lumBlkTmp[i][0]);
		}//end if tmpBlkFlag...
		else
		{
			pDcLumBlk = &((pMb->_lumDcBlk.GetBlk())[4 * i + 3]);	///< Short cuts.
			pLumBlk = &(pMb->_lumBlk[i][0]);
		}//end else...

		/// Inverse scale and quant.
		_pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, mbLumQP);
		_pI4x4TLum->SetMode(IInverseTransform::QuantOnly);
		(pLumBlk++)->InverseQuantise(_pI4x4TLum);	///< [i][0]
		(pLumBlk++)->InverseQuantise(_pI4x4TLum);	///< [i][1]
		(pLumBlk++)->InverseQuantise(_pI4x4TLum);	///< [i][2]
		pLumBlk->InverseQuantise(_pI4x4TLum);			///< [i][3]
		_pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
		_pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
		pCbBlk->InverseQuantise(_pI4x4TChr);
		pCrBlk->InverseQuantise(_pI4x4TChr);

		/// Inverse transform without scaling or quant.
		_pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
		_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);

		pLumBlk->SetDC(*pDcLumBlk--);						///< Load the DC term from the DC block.
		(pLumBlk--)->InverseTransform(_pI4x4TLum);	///< [i][3]

		pLumBlk->SetDC(*pDcLumBlk--);
		(pLumBlk--)->InverseTransform(_pI4x4TLum);	///< [i][2]

		pLumBlk->SetDC(*pDcLumBlk--);
		(pLumBlk--)->InverseTransform(_pI4x4TLum);	///< [i][1]

		pLumBlk->SetDC(*pDcLumBlk);
		pLumBlk->InverseTransform(_pI4x4TLum);			///< [i][0]

		pCbBlk->SetDC(*pDcCbBlk++);
		(pCbBlk++)->InverseTransform(_pI4x4TChr);	///< [i]

		pCrBlk->SetDC(*pDcCrBlk++);
		(pCrBlk++)->InverseTransform(_pI4x4TChr);	///< [i]
	}//end for i...

}//end InverseTransAndQuantIntra16x16MBlk.

/** Inverse Quantise and Transform an Intra_16x16 mode block.
Code refactoring method to inverse quantise the block and set the DC coeff before the inverse
transform. The inverse transform must have the quantisation parameter preset before calling
this method.
@param pTQ					: Inverse Transform to use.
@param pBlk					: Blk to process.
@param pDcBlkCoeff	: reference to get the DC coeff.
@return							: none.
*/
void H264v2Codec::InvTransAndQuantIntra16x16ModeBlk(IInverseTransform* pTQ, BlockH264* pBlk, short* pDcBlkCoeff)
{
	pTQ->SetMode(IInverseTransform::QuantOnly);
	pBlk->InverseQuantise(pTQ);
	pBlk->SetDC(*pDcBlkCoeff);	///< Set the DC coeff for this block from the DC block.
	pTQ->SetMode(IInverseTransform::TransformOnly);
	pBlk->InverseTransform(pTQ);
}//end InvTransAndQuantIntra16x16ModeBlk.

/** Transform and Quantise an Inter_16x16 macroblock
This method provides a speed improvement for macroblock processing and code refactoring.
@param pMb	: Macroblock to transform.
@return			: none
*/
void H264v2Codec::TransAndQuantInter16x16MBlk(MacroBlockH264* pMb)
{
	int i;
	int mbLumQP = pMb->_mbQP;
	int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);

	/// The transform (mode = TransformOnly) and quantisation (mode = QuantOnly) are separated 
	/// for the chr 4x4 AC blocks in this method. The lum and DC blocks are in the 
  /// TransformAndQuant mode and require the setting.
	_pF4x4TLum->SetMode(IForwardTransform::TransformAndQuant);
	_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, mbLumQP);
	_pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	_pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);

	/// Do the forward 4x4 transform on the non-DC 4x4 blocks without scaling or quantisation. Pull
	/// out the DC terms from each chr block and populate the DC blocks and then scale and quant the
	/// non-DC 4x4 blocks. Note that this is done in raster scan order and not coding order to
	/// align each position in the DC block with the spatial location of the 4x4 block in the image.
	BlockH264*	pCbBlk = &(pMb->_cbBlk[0][0]);	///< Assume these are linear arrays that wrap in raster scan order.
	BlockH264*	pCrBlk = &(pMb->_crBlk[0][0]);
	short*			pDcCbBlk = pMb->_cbDcBlk.GetBlk();
	short*			pDcCrBlk = pMb->_crDcBlk.GetBlk();
	for (i = 0; i < 4; i++)
	{
		/// Unroll the inner loop.

		BlockH264*	pLumBlk = &(pMb->_lumBlk[i][0]);	///< Short cuts.

		/// Transform Lum with scaling and quant.
		(pLumBlk++)->ForwardTransform(_pF4x4TLum);	///< [i][0]
		(pLumBlk++)->ForwardTransform(_pF4x4TLum);	///< [i][1]
		(pLumBlk++)->ForwardTransform(_pF4x4TLum);	///< [i][2]
		pLumBlk->ForwardTransform(_pF4x4TLum);			///< [i][3]

	  /// For chr, split the transform and quant processes. 
		_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);

		pCbBlk->ForwardTransform(_pF4x4TChr);	///< [i]
		*pDcCbBlk++ = pCbBlk->GetDC();
		pCbBlk->SetDC(0);

		pCrBlk->ForwardTransform(_pF4x4TChr);	///< [i]
		*pDcCrBlk++ = pCrBlk->GetDC();
		pCrBlk->SetDC(0);

		/// Now scale and quant chr.
		_pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
		(pCbBlk++)->Quantise(_pF4x4TChr);
		(pCrBlk++)->Quantise(_pF4x4TChr);

	}//end for i...

	/// Transform and quant the DC blocks.
	pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
	pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

}//end TransAndQuantInter16x16MBlk.

/** Transform and Quantise an Inter_16x16 macroblock with a Dmax criterion.
This method provides a speed improvement for macroblock processing and code refactoring. The
QP value is decremented until the mb Lum distortion is below Dmax. The quatised coeff are stored
in the _lumBlk[][], etc. and the inverse reconstructed pels are stored in the _lumBlkTmp[][], etc.
@param pMb	  : Macroblock to transform.
@param Dmax   : Max distortion for this mb.
@param minQP  : Lower limit for the QP descent.
@return			  : Distortion for the mb.
*/
int H264v2Codec::TransAndQuantInter16x16MBlk(MacroBlockH264* pMb, int Dmax, int minQP)
{
	int i, j;
	int distortion = 0;
	int qp = pMb->_mbQP;
	int eqp = pMb->_mbEncQP;
	if (eqp > (H264V2_MAX_QP + 16))  ///< Only consider lum coeffs.
		eqp = H264V2_MAX_QP + 16;

	/// ----------------------------- Lum -----------------------------------------------------------
	/// Transform without quant and store. This is then used as the starting storage
	/// point to reload on each iteration of the Dmax descent. The main blks are only quantised 
	/// when the final QP value is determined.
	_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
	_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	for (i = 0; i < 4; i++)
		for (j = 0; j < 2; j++) ///< 2 at a time
		{

      short* pCoeff = pMb->_lumBlk[i][j].GetBlk();
      short* pTmp   = _tmpBlk[i][j].GetBlk();
      /// Copy to temp and then do in-place forward dct transform on coeff.
      memcpy((void *)pTmp, (void *)pCoeff, sizeof(short) * 16);
      _pF4x4TLum->Transform( (void *)pCoeff);

      short* pCoeff2 = pMb->_lumBlk[i][j+2].GetBlk();
      short* pTmp2 = _tmpBlk[i][j+2].GetBlk();
      /// Copy to temp and then do in-place forward dct transform on coeff.
      memcpy((void *)pTmp2, (void *)pCoeff2, sizeof(short) * 16);
      _pF4x4TLum->Transform((void *)pCoeff2);

/*
			BlockH264* pb = &(pMb->_lumBlk[i][j]);
			/// Make a copy of the input lum blks for later distortion calculations. These input
		  /// blks enter this method holding the diff between the input and the motion pred images.
			pb->Copy(_tmpBlk[i][j].GetBlk());
			/// DCT in-place but no quant.
			pb->ForwardTransform(_pF4x4TLum);
*/
		}//end for i & j...

	/// Iterate QP down from qp to either distortion <= Dmax or minQP (without distortion <= Dmax is best 
	/// that can be done).

  /// Quant only for the temp Lum blks.
  _pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
  /// Invese quant and transform.
  _pI4x4TLum->SetMode(IInverseTransform::TransformAndQuant);

  /// An interval halving algorithm with limiting the change in QP range from the previous mb.
  int low = minQP;
  int high = qp;
  int fivePercent = ((Dmax * 5) + 50) / 100;

  bool close  = false;

  /// Impose a limit on delta QP from previous mb QP.
  int prevQP = GetPrevMbEncQP(pMb);
  qp = SetQPRangeLimits(qp, prevQP, 12, minQP, H264V2_MAX_QP, &high, &low);

  if (_modeOfOperation == H264V2_MINAVG_RATECNT)
  {
    /// The davg value is already set as that required for the remaining mbs from this point. If this
    /// mb is the 1st mb then calibrate the QP to match davg. For all other mbs, predict a QP based 
    /// on previous mbs to target davg.
    int davg = Dmax;    ///< davg is in units of square error per mb.
    int tenPercent = ((Dmax * 10) + 50) / 100;
    qp = eqp = prevQP;  ///< Best starting QP is from the previous mb.

    if (pMb->_mbIndex == 0) ///< Calibrate QP to davg on the 1st mb.
    {
      do  ///< Until high-low gap is small or within 5% of davg
      {
        /// Forward quant and inverse quant and transform. Accumulate ROI distortion.
        distortion = Copy_Q_IQ_IT(pMb, qp, eqp); ///< Code re-factoring method
        /// Update mb QP with the last values used in the while loop
        pMb->_mbQP = pMb->_mbEncQP = qp;

        /// Set the new interval boundaries
        if (davg >= distortion) low = qp;
        else high = qp;
        /// New qp.
        qp = (low + high) / 2;  ///< Halve the interval.
        eqp = qp;

        close = abs(davg - distortion) < fivePercent;

      } while (((high - low) > 1) && !close); //end do while high...
    }//end if !_mbIndex...
    else  ///< else all other mbs...
    {
      /// QP prediction from davg.
      qp = GetPredQPStepMethod(pMb, davg, tenPercent, low, high);

      /// Forward quant and inverse quant and transform. Accumulate ROI distortion.
      eqp = qp;
      distortion = Copy_Q_IQ_IT(pMb, qp, eqp); ///< Code re-factoring method
      /// Update mb QP with the last values used.
      pMb->_mbQP = pMb->_mbEncQP = qp;
    }//end else...

  }//end if H264V2_MINAVG_RATECNT...
  else ///< if(_modeOfOperation == H264V2_MINMAX_RATECNT)
  {
    do ///< Until high-low gap is small, within 5% of Dmax or no change in distortion
    {
      /// Forward quant and inverse quant and transform. Accumulate ROI distortion.
      distortion = Copy_Q_IQ_IT(pMb, qp, eqp); ///< Code re-factoring method
      /// Update mb QP with the last values used in the while loop
      pMb->_mbQP = pMb->_mbEncQP = qp;

      /// Set the new interval boundaries
      if (Dmax >= distortion) low = qp;
      else high = qp;
      /// New qp.
      qp = (low + high) / 2;  ///< Halve the interval.
      eqp = qp;

      int diff = (Dmax - distortion);
      close = (diff > 0) && (diff < fivePercent);

    } while (((high - low) > 3) && !close); //end do while high...
//    } while (((high - low) > 1) && !close); //end do while high...

    /// Check to make sure the selected QP does result in a lower distortion than Dmax. Otherwise
    /// use the last low boundary qp value.
    if (Dmax < distortion)
    {
      distortion = Copy_Q_IQ_IT(pMb, low, low);
      /// Update mb QP with the last values used
      pMb->_mbQP = pMb->_mbEncQP = low;
    }//end if Dmax...
  }//end else if H264V2_MINMAX_RATECNT...

	/// Finally quant the lum blks.
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			pMb->_lumBlk[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
			/// Zero coeffs for extended EncQP range.
			if (pMb->_mbEncQP > H264V2_MAX_QP)
				pMb->_lumBlk[i][j].Zero(16 - (pMb->_mbEncQP - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);
		}//end for i & j...

	/// ----------------------------- Chr -----------------------------------------------------------
	/// Chr is processed after the QP value for this block has been determined by the Lum process.
	int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);
	/// Forward.
	_pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	_pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
	/// Inverse.
	_pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
	_pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

	short* pDcCbBlk = pMb->_cbDcBlk.GetBlk();
	short* pDcCrBlk = pMb->_crDcBlk.GetBlk();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
		{
			BlockH264*  pCb = &(pMb->_cbBlk[i][j]);
			BlockH264*  pCr = &(pMb->_crBlk[i][j]);

			_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
			/// Cb
			pCb->ForwardTransform(_pF4x4TChr);  ///< DCT.
			*pDcCbBlk++ = pCb->GetDC();         ///< Set DC blk
			pCb->SetDC(0);                      ///< Clear DC term in AC blk.

			/// Cr
			pCr->ForwardTransform(_pF4x4TChr);  ///< DCT.
			*pDcCrBlk++ = pCr->GetDC();         ///< Set DC blk
			pCr->SetDC(0);                      ///< Clear DC term in AC blk.

			_pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
			pCb->Quantise(_pF4x4TChr);         ///< Scale and quant.
			pCr->Quantise(_pF4x4TChr);

			/// Follow the lum extended QP range on the Chr AC coeffs.
			if (pMb->_mbEncQP > H264V2_MAX_QP)
			{
				pCb->Zero(16 - (pMb->_mbEncQP - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);
				pCr->Zero(16 - (pMb->_mbEncQP - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);
			}//end if _mbEncQP...
		}//end for i & j...

	/// Transform and quant the DC blocks.
	pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
	pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

	/// Copy the Chr blks into the temp blks for inverse processing.
	MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_CB_DC, MBH264_CR_1_1);

	/// Inverse quant and inverse transform the DC blks.
	pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
	pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

	/// Inverse quant then add DC term before inverse transform.
	short* pDcCbBlkTmp = pMb->_cbDcBlkTmp.GetBlk();
	short* pDcCrBlkTmp = pMb->_crDcBlkTmp.GetBlk();
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
		{
			BlockH264*  pCbT = &(pMb->_cbBlkTmp[i][j]);
			BlockH264*  pCrT = &(pMb->_crBlkTmp[i][j]);

			_pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
			pCbT->InverseQuantise(_pI4x4TChr);  ///< Inverse scale and quant.
			pCbT->SetDC(*pDcCbBlkTmp++);        ///< Load the DC term from the DC blk.
			pCrT->InverseQuantise(_pI4x4TChr);
			pCrT->SetDC(*pDcCrBlkTmp++);

			_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
			pCbT->InverseTransform(_pI4x4TChr);  ///< Inverse transform.
			pCrT->InverseTransform(_pI4x4TChr);
		}//end for i & j...

	return(distortion);
}//end TransAndQuantInter16x16MBlk.

/** Code Refactoring: Used in the TransAndQuantInter16x16MBlk() method
Accumulate mb distortion from copying blks to tmp blks, Quant, Inverse Quant and 
Inverse transform of the _lumBlkTmp[][].
@param pMb    : Macroblock to operate on
@param eqp    : QP
@param eqp    : Encoded set QP
@return       : distortion.
*/
int H264v2Codec::Copy_Q_IQ_IT(MacroBlockH264* pMb, int qp, int eqp)
{
  int distortion = 0;
  /// Forward.
  _pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
  /// Inverse
  _pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, qp);
  /// Copy the Lum blks into the temp Lum blks for forward quant and inverse transform + quant processing.
  MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_0_0, MBH264_LUM_3_3);

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
    {
      BlockH264* pb = &(pMb->_lumBlkTmp[i][j]);
      pb->ForwardTransform(_pF4x4TLum); ///< Forward quant.

      if (eqp > H264V2_MAX_QP) ///< For extended EncQP range [52..67], zero coeffs.
        pb->Zero(16 - (eqp - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);

      pb->InverseTransform(_pI4x4TLum);	///< Inverse scale, quant and transform.
      /// Total square diff with the input store.
#ifdef USE_ABSOLUTE_DIFFERENCE
      distortion += pb->GetBlkOverlay()->Tad4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#else
      distortion += pb->GetBlkOverlay()->Tsd4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#endif
    }//end for i & j...

     /// Region of interest multiplier application to the whole mb.
  distortion = ROIDistortion(pMb->_mbIndex, distortion);

  return(distortion);
}//end Copy_Q_IQ_IT.

 /** Inverse Transform and Quantise an Inter_16x16 macroblock
This method provides a speed improvement for macroblock processing and code refactoring.
@param pMb				: Macroblock to inverse transform.
@param tmpBlkFlag	: Indicate temp blocks to be used.
@return						: none
*/
void H264v2Codec::InverseTransAndQuantInter16x16MBlk(MacroBlockH264* pMb, int tmpBlkFlag)
{
	int					i;
	BlockH264*	pLumBlk;
	BlockH264*	pCbBlk;
	BlockH264*	pCrBlk;
	short*			pDcCbBlk;
	short*			pDcCrBlk;
	int					mbLumQP = pMb->_mbQP;
	int					mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);

	/// The inverse transform (mode = TransformOnly) and quantisation (mode = QuantOnly) are separated 
	/// for the 4x4 AC blocks in this method and therefore the quant parameter does not need to 
	/// be set. But the DC blocks are in the TransformAndQuant mode and require the setting.
	_pI4x4TLum->SetMode(IInverseTransform::TransformAndQuant);
	_pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, mbLumQP);
	_pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
	_pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

	if (tmpBlkFlag)
	{
		/// Copy all blks, excluding Lum DC blk, to temp blks.
		MacroBlockH264::CopyBlksToTmpBlksCoeffOnly(pMb, 1, MBH264_NUM_BLKS - 1);

		/// Inverse transform & inverse quantise DC blocks.
		pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
		pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

		/// Short cut pointers for Lum & Chr components.
		pLumBlk = &(pMb->_lumBlkTmp[0][0]);
		pCbBlk = &(pMb->_cbBlkTmp[0][0]);	///< Assume these are linear arrays that wrap in raster scan order.
		pCrBlk = &(pMb->_crBlkTmp[0][0]);
		pDcCbBlk = pMb->_cbDcBlkTmp.GetBlk();	///< Actual array of coeffs.
		pDcCrBlk = pMb->_crDcBlkTmp.GetBlk();
	}//end if tmpBlkFlag...
	else
	{
		/// Inverse transform & inverse quantise DC blocks.
		pMb->_cbDcBlk.InverseTransform(_pIDC2x2T);
		pMb->_crDcBlk.InverseTransform(_pIDC2x2T);

		/// Short cut pointers for Chr components.
		pLumBlk = &(pMb->_lumBlk[0][0]);
		pCbBlk = &(pMb->_cbBlk[0][0]);	///< Assume these are linear arrays that wrap in raster scan order.
		pCrBlk = &(pMb->_crBlk[0][0]);
		pDcCbBlk = pMb->_cbDcBlk.GetBlk();	///< Actual array of coeffs.
		pDcCrBlk = pMb->_crDcBlk.GetBlk();
	}//end else...

	/// Inverse quant for the main Lum, Cb and Cr blocks, insert the DC terms from the 
	/// DC blocks and then complete the inverse transform process. Note that this is 
	/// done in raster scan order and not coding order to align each position in the 
	/// DC block with the spatial location of the 4x4 block in the image.

	for (i = 0; i < 4; i++)	///< 16 Lum blocks + 4 Cb Chr and 4 Cr Chr blocks.
	{
		/// Unroll the inner loop.

		/// Inverse scale, quant and transform Lum.
		(pLumBlk++)->InverseTransform(_pI4x4TLum);	///< [i][0]
		(pLumBlk++)->InverseTransform(_pI4x4TLum);	///< [i][1]
		(pLumBlk++)->InverseTransform(_pI4x4TLum);	///< [i][2]
		(pLumBlk++)->InverseTransform(_pI4x4TLum);	///< [i][3]

	/// Inverse scale and quant first for Chr.
		_pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
		pCbBlk->InverseQuantise(_pI4x4TChr);
		pCrBlk->InverseQuantise(_pI4x4TChr);

		/// Inverse transform after scaling and quant.
		_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
		pCbBlk->SetDC(*pDcCbBlk++);
		(pCbBlk++)->InverseTransform(_pI4x4TChr);	///< [i]

		pCrBlk->SetDC(*pDcCrBlk++);
		(pCrBlk++)->InverseTransform(_pI4x4TChr);	///< [i]
	}//end for i...

}//end InverseTransAndQuantInter16x16MBlk.

/** Get Intra Lum prediction.
Select the best prediction mode based on the min distortion with the input img. Predict
from the ref lum img and write it to the pred parameter. Used by the encoder. Return the
intra_16x16 prediction mode.
@param pMb				: Macroblock to predict.
@param in					: Lum input image.
@param ref				: Reference lum img to predict from.
@param pred				: 16x16 overlay to write result to.
@return						: Prediction mode.
*/
int H264v2Codec::GetIntra16x16LumPredAndMode(MacroBlockH264* pMb, OverlayMem2Dv2* in, OverlayMem2Dv2* ref, OverlayMem2Dv2* pred)
{
	int i, j;
	int a, b, c;	///< Plane mode intermediates.
	int predDC;	///< Other mode intermediates.
	int modeDist[4] = { 0, 0, 0, 0 }; ///< Accumulator for the distortion tests.

	int mode = MacroBlockH264::Intra_16x16_DC;

	short**	in2D = in->Get2DSrcPtr();
	int			iOffX = in->GetOriginX();
	int			iOffY = in->GetOriginY();
	short**	ref2D = ref->Get2DSrcPtr();
	int			rOffX = ref->GetOriginX();
	int			rOffY = ref->GetOriginY();
	short**	pred2D = pred->Get2DSrcPtr();
	int			pOffX = pred->GetOriginX();
	int			pOffY = pred->GetOriginY();

	/// Every mode has intermediate values that the predictions are based on and
	/// these must be prepared first. The values are further dependent on the 
	/// availability of the neighbourhood.
	bool all = (pMb->_aboveMb != NULL) && (pMb->_aboveLeftMb != NULL) && (pMb->_leftMb != NULL);
	bool aboveOnly = (pMb->_aboveMb != NULL) && (pMb->_leftMb == NULL);	///< Don't care about above left.
	bool leftOnly = (pMb->_aboveMb == NULL) && (pMb->_leftMb != NULL);	///< Don't care about above left.
	bool aboveAndLeft = (pMb->_aboveMb != NULL) && (pMb->_leftMb != NULL);	///< Don't care about above left.

	/// The selection process uses a patial sum of the distortion method where a grid of sampling points
	/// is tested with new sets of points at each iteration. If there is no change in the winning mode
	/// after the iteration then that mode is selected.
	int pos = 0;	///< Position in the test sample point array. (Accumulates after each iteration.)
	int noChange = 0;

	if (all)	///< Every mode is a candidate.
	{
		///------------------- DC mode preparation -----------------------------------------
		predDC = 0;
		for (i = 0; i < 16; i++)
			predDC += ((int)ref2D[rOffY - 1][rOffX + i] + (int)ref2D[rOffY + i][rOffX - 1]);
		predDC = (predDC + 16) >> 5;

		///------------------- Plane mode preparation --------------------------------------
		int		H = 0;
		int		V = 0;
		for (i = 0; i < 8; i++)
		{
			H += (i + 1)*((int)ref2D[rOffY - 1][rOffX + 8 + i] - (int)ref2D[rOffY - 1][rOffX + 6 - i]);
			V += (i + 1)*((int)ref2D[rOffY + 8 + i][rOffX - 1] - (int)ref2D[rOffY + 6 - i][rOffX - 1]);
		}//end for i...
		b = (5 * H + 32) >> 6;
		c = (5 * V + 32) >> 6;
		a = ((int)ref2D[rOffY + 15][rOffX - 1] + (int)ref2D[rOffY - 1][rOffX + 15]) << 4;

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_16x16_PATH_LENGTH; pos++)
		{
			int x = testPntPath16xy[pos][0];
			int y = testPntPath16xy[pos][1];

			/// Accumulate mode distortions here for this sampling point.
				  /// Vertical mode.
			modeDist[MacroBlockH264::Intra_16x16_Vert] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], (int)ref2D[rOffY - 1][rOffX + x]);
			/// Horiz mode.
			modeDist[MacroBlockH264::Intra_16x16_Horiz] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], (int)ref2D[rOffY + y][rOffX - 1]);
			/// DC mode.
			modeDist[MacroBlockH264::Intra_16x16_DC] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], predDC);
			/// Plane mode.
			modeDist[MacroBlockH264::Intra_16x16_Plane] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], H264V2_CLIP255((a + b*(x - 7) + c*(y - 7) + 16) >> 5));
		}//end for pos...

		mode = MacroBlockH264::Intra_16x16_Vert;	///< MacroBlockH264::Intra_16x16_Vert = 0.
		for (i = MacroBlockH264::Intra_16x16_Horiz; i < 4; i++)	///< Total of 4 modes.
		{
			if (modeDist[i] < modeDist[mode])
				mode = i;
		}//end for i...

	}//end if all...
	else if (aboveAndLeft)
	{
		///------------------- DC mode preparation -----------------------------------------
		predDC = 0;
		for (i = 0; i < 16; i++)
			predDC += ((int)ref2D[rOffY - 1][rOffX + i] + (int)ref2D[rOffY + i][rOffX - 1]);
		predDC = (predDC + 16) >> 5;

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_16x16_PATH_LENGTH; pos++)
		{
			int x = testPntPath16xy[pos][0];
			int y = testPntPath16xy[pos][1];

			/// Accumulate mode distortions here for this sampling point.
				  /// Vertical mode.
			modeDist[MacroBlockH264::Intra_16x16_Vert] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], (int)ref2D[rOffY - 1][rOffX + x]);
			/// Horiz mode.
			modeDist[MacroBlockH264::Intra_16x16_Horiz] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], (int)ref2D[rOffY + y][rOffX - 1]);
			/// DC mode.
			modeDist[MacroBlockH264::Intra_16x16_DC] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], predDC);
		}//end for pos...

		mode = MacroBlockH264::Intra_16x16_Vert;	///< MacroBlockH264::Intra_16x16_Vert = 0.
		for (i = MacroBlockH264::Intra_16x16_Horiz; i < 3; i++)	///< Total of 3 modes. Plane mode excluded.
		{
			if (modeDist[i] < modeDist[mode])
				mode = i;
		}//end for i...

	}//end else if aboveAndLeft...
	else if (leftOnly)
	{
		///------------------- DC mode preparation -----------------------------------------
		predDC = 0;
		for (i = 0; i < 16; i++)
			predDC += (int)ref2D[rOffY + i][rOffX - 1];
		predDC = (predDC + 8) >> 4;

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_16x16_PATH_LENGTH; pos++)
		{
			int x = testPntPath16xy[pos][0];
			int y = testPntPath16xy[pos][1];

			/// Accumulate mode distortions here for this sampling point.
				  /// Horiz mode.
			modeDist[MacroBlockH264::Intra_16x16_Horiz] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], (int)ref2D[rOffY + y][rOffX - 1]);
			/// DC mode.
			modeDist[MacroBlockH264::Intra_16x16_DC] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], predDC);
		}//end for pos...

		mode = MacroBlockH264::Intra_16x16_Horiz;	///< MacroBlockH264::Intra_16x16_Horiz = 0.
		if (modeDist[MacroBlockH264::Intra_16x16_DC] < modeDist[MacroBlockH264::Intra_16x16_Horiz])
			mode = MacroBlockH264::Intra_16x16_DC;

	}//end else if leftOnly...
	else if (aboveOnly)
	{
		///------------------- DC mode preparation -----------------------------------------
		predDC = 0;
		for (i = 0; i < 16; i++)
			predDC += (int)ref2D[rOffY - 1][rOffX + i];
		predDC = (predDC + 8) >> 4;

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_16x16_PATH_LENGTH; pos++)
		{
			int x = testPntPath16xy[pos][0];
			int y = testPntPath16xy[pos][1];

			/// Accumulate mode distortions here for this sampling point.
				  /// Vertical mode.
			modeDist[MacroBlockH264::Intra_16x16_Vert] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], (int)ref2D[rOffY - 1][rOffX + x]);
			/// DC mode.
			modeDist[MacroBlockH264::Intra_16x16_DC] += DISTORTIONSQRDIFF((int)in2D[iOffY + y][iOffX + x], predDC);
		}//end for pos...

		mode = MacroBlockH264::Intra_16x16_Vert;	///< MacroBlockH264::Intra_16x16_Vert = 0.
		if (modeDist[MacroBlockH264::Intra_16x16_DC] < modeDist[MacroBlockH264::Intra_16x16_Vert])
			mode = MacroBlockH264::Intra_16x16_DC;

	}//end else if aboveOnly...
	else
	{
		/// Use the DC default mode when there are no valid neighbours and use half the 
		/// max Lum value (128) as the prediction. This is typically the top left corner
		/// of the slice.
		pred->Fill(128);
		return(mode);	///< Early exit.
	}//end else...

	/// Fill the prediction block with the lowest distortion mode prediction.
	switch (mode)
	{
	case MacroBlockH264::Intra_16x16_Vert:
	{
		for (i = 0; i < 16; i++)
			for (j = 0; j < 16; j++)
				pred2D[pOffY + i][pOffX + j] = ref2D[rOffY - 1][rOffX + j];
	}//end Vert block...
	break;
	case MacroBlockH264::Intra_16x16_Horiz:
	{
		for (i = 0; i < 16; i++)
			for (j = 0; j < 16; j++)
				pred2D[pOffY + i][pOffX + j] = ref2D[rOffY + i][rOffX - 1];
	}//end Horiz block...
	break;
	case MacroBlockH264::Intra_16x16_DC:
		pred->Fill(predDC);
		break;
	case MacroBlockH264::Intra_16x16_Plane:
	{
		for (i = 0; i < 16; i++)
			for (j = 0; j < 16; j++)
				pred2D[pOffY + i][pOffX + j] = H264V2_CLIP255((a + b*(j - 7) + c*(i - 7) + 16) >> 5);
	}//end Horiz block...
	break;
	}//end switch mode...

	return(mode);
}//end GetIntra16x16LumPredAndMode.

/** Get Intra Lum prediction.
Apply the prediction mode specified to predict from the ref lum img and write it to the pred
parameter. Used by the decoder.
@param pMb				: Macroblock to predict.
@param ref				: Reference lum img to predict from.
@param pred				: 16x16 overlay to write result to.
@param predMode		: Mode to apply.
@return						: 1 = success, 0 = failure.
*/
int H264v2Codec::GetIntra16x16LumPred(MacroBlockH264* pMb, OverlayMem2Dv2* ref, OverlayMem2Dv2* pred, int predMode)
{
	int i, j;

	short**	ref2D = ref->Get2DSrcPtr();
	int			rOffX = ref->GetOriginX();
	int			rOffY = ref->GetOriginY();
	short**	pred2D = pred->Get2DSrcPtr();
	int			pOffX = pred->GetOriginX();
	int			pOffY = pred->GetOriginY();

	/// Fill the prediction block with the appropriate mode prediction.
	switch (predMode)
	{
	case MacroBlockH264::Intra_16x16_Vert:
	{
		if (pMb->_aboveMb != NULL)	///< Vert prediction only cares about the macroblock above.
		{
			for (i = 0; i < 16; i++)
				for (j = 0; j < 16; j++)
					pred2D[pOffY + i][pOffX + j] = ref2D[rOffY - 1][rOffX + j];
		}//end if aboveMb...
		else
		{
			/// Prediction fails.
			pred->Fill(128);	///< Safty net to fill with half max value.
			return(0);
		}//end else...
	}//end Vert block...
	break;
	case MacroBlockH264::Intra_16x16_Horiz:
	{
		if (pMb->_leftMb != NULL)	///< Horiz prediction only cares about the macroblock to the left.
		{
			for (i = 0; i < 16; i++)
				for (j = 0; j < 16; j++)
					pred2D[pOffY + i][pOffX + j] = ref2D[rOffY + i][rOffX - 1];
		}//end if leftMb...
		else
		{
			/// Prediction fails.
			pred->Fill(128);	///< Safty net to fill with half max value.
			return(0);
		}//end else...
	}//end Horiz block...
	break;
	case MacroBlockH264::Intra_16x16_DC:
	{
		bool aboveAndLeft = (pMb->_aboveMb != NULL) && (pMb->_leftMb != NULL);
		bool aboveOnly = (pMb->_aboveMb != NULL) && (pMb->_leftMb == NULL);
		bool leftOnly = (pMb->_aboveMb == NULL) && (pMb->_leftMb != NULL);

		int predDC = 0;
		if (aboveAndLeft)
		{
			for (i = 0; i < 16; i++)
				predDC += ((int)ref2D[rOffY - 1][rOffX + i] + (int)ref2D[rOffY + i][rOffX - 1]);
			predDC = (predDC + 16) >> 5;
		}//end if aboveAndLeft...
		else if (leftOnly)
		{
			for (i = 0; i < 16; i++)
				predDC += (int)ref2D[rOffY + i][rOffX - 1];
			predDC = (predDC + 8) >> 4;
		}//end else if leftOnly...
		else if (aboveOnly)
		{
			for (i = 0; i < 16; i++)
				predDC += (int)ref2D[rOffY - 1][rOffX + i];
			predDC = (predDC + 8) >> 4;
		}//end else if aboveOnly...
		else
		{
			predDC = 128;	///< Half the max value.
		}//end else...

		pred->Fill(predDC);
	}//end DC block...
	break;
	case MacroBlockH264::Intra_16x16_Plane:
	{
		bool all = (pMb->_aboveMb != NULL) && (pMb->_aboveLeftMb != NULL) && (pMb->_leftMb != NULL);

		if (all)	///< All the neighbouring macroblocks must be available for prediction.
		{
			int		H = 0;
			int		V = 0;
			for (i = 0; i < 8; i++)
			{
				H += (i + 1)*((int)ref2D[rOffY - 1][rOffX + 8 + i] - (int)ref2D[rOffY - 1][rOffX + 6 - i]);
				V += (i + 1)*((int)ref2D[rOffY + 8 + i][rOffX - 1] - (int)ref2D[rOffY + 6 - i][rOffX - 1]);
			}//end for i...
			int b = (5 * H + 32) >> 6;
			int c = (5 * V + 32) >> 6;
			int a = ((int)ref2D[rOffY + 15][rOffX - 1] + (int)ref2D[rOffY - 1][rOffX + 15]) << 4;

			for (i = 0; i < 16; i++)
				for (j = 0; j < 16; j++)
					pred2D[pOffY + i][pOffX + j] = H264V2_CLIP255((a + b*(j - 7) + c*(i - 7) + 16) >> 5);
		}//end if all...
		else
		{
			/// Prediction fails.
			pred->Fill(128);	///< Safty net to fill with half max value.
			return(0);
		}//end else...
	}//end Horiz block...
	break;
	}//end switch predMode...

	return(1);
}//end GetIntra16x16LumPred.

/** Get Intra Lum DC prediction.
Predict from the input Lum img and write it to the pred parameter. If
there are no valid neighbours to make the prediction then use half the
max Lum value (128) as the prediction.
@param pMb				: Macroblock to transform.
@param lum				: Lum img to predict from.
@param pred				: 16x16 overlay to write result to.
@return						: none
*/
void H264v2Codec::GetIntra16x16LumDCPred(MacroBlockH264* pMb, OverlayMem2Dv2* lum, OverlayMem2Dv2* pred)
{
	int offX, offY, i;
	int dirCnt = 0;
	int predValue = 0;

	short** img = lum->Get2DSrcPtr();

	if (pMb->_aboveMb != NULL)
	{
		dirCnt++;
		/// The last row of the macroblock above.
		offX = pMb->_aboveMb->_offLumX;
		offY = pMb->_aboveMb->_offLumY + 15;
		for (i = 0; i < 16; i++)
			predValue += img[offY][offX + i];
	}//end if _aboveMb...

	if (pMb->_leftMb != NULL)
	{
		dirCnt++;
		/// The last col of the macroblock to the left.
		offX = pMb->_leftMb->_offLumX + 15;
		offY = pMb->_leftMb->_offLumY;
		for (i = 0; i < 16; i++)
			predValue += img[offY + i][offX];
	}//end if _leftMb...

	/// Set the accumulated DC prediction value.
	if (dirCnt == 2)
		predValue = (predValue + 16) >> 5;
	else if (dirCnt == 1)
		predValue = (predValue + 8) >> 4;
	else
		predValue = 128;	///< Half max Lum value.

	pred->Fill(predValue);

}//end GetIntra16x16LumDCPred.

/** Get Intra Lum Plane prediction.
Predict from the input Lum img and write it to the pred parameter. If
there are no valid neighbours to make the prediction then use half the
max Lum value (128) as the prediction. The H.264 std does not specify
this last case for Plane pred.
@param pMb				: Macroblock to transform.
@param lum				: Lum img to predict from.
@param pred				: 16x16 overlay to write result to.
@return						: 1 = pred was made, 0 = no macroblocks available for pred.
*/
int H264v2Codec::GetIntra16x16LumPlanePred(MacroBlockH264* pMb, OverlayMem2Dv2* lum, OverlayMem2Dv2* pred)
{
	int i, j;

	if ((pMb->_aboveMb != NULL) && (pMb->_aboveLeftMb != NULL) && (pMb->_leftMb != NULL))
	{
		short**	img2D = lum->Get2DSrcPtr();
		int			iOffX = lum->GetOriginX();	///< Top left of img2D pMb location.
		int			iOffY = lum->GetOriginY();
		short**	pred2D = pred->Get2DSrcPtr();
		int			pOffX = pred->GetOriginX();
		int			pOffY = pred->GetOriginY();

		int		H = 0;
		int		V = 0;
		for (i = 0; i < 8; i++)
		{
			H += (i + 1)*((int)img2D[iOffY - 1][iOffX + 8 + i] - (int)img2D[iOffY - 1][iOffX + 6 - i]);
			V += (i + 1)*((int)img2D[iOffY + 8 + i][iOffX - 1] - (int)img2D[iOffY + 6 - i][iOffX - 1]);
		}//end for i...
		int b = (5 * H + 32) >> 6;
		int c = (5 * V + 32) >> 6;
		int a = ((int)img2D[iOffY + 15][iOffX - 1] + (int)img2D[iOffY - 1][iOffX + 15]) << 4;

		for (i = 0; i < 16; i++)
			for (j = 0; j < 16; j++)
			{
				short x = (short)((a + b*(j - 7) + c*(i - 7) + 16) >> 5);
				pred2D[pOffY + i][pOffX + j] = (short)(H264V2_CLIP255(x));
			}//end for i & j...

		return(1);	///< Successful prediction.
	}//end if _aboveMb...

	/// Safety precaution not defined in H.264 standard. If the macroblock above, 
	/// above left and left are not available for prediction then this mode should 
	/// never be used.
	pred->Fill(128);
	return(0);	///< Prediction macroblocks do not exist.
}//end GetIntra16x16LumPlanePred.

/** Get Intra Vertical prediction.
Predict from the input img and write it to the pred parameter. The dimensions
of the pred block define the size of the operation. If there are no valid
neighbours to make the prediction then use half the max value (128) as
the prediction. The H.264 std does not specify this last case for vert pred.
@param pMb				: Macroblock to transform.
@param img				: Img to predict from.
@param pred				: Overlay to write prediction result to.
@param lumFlag		: Is the prediction for lum?
@return						: 1 = pred was made, 0 = no macroblocks available for pred.
*/
int H264v2Codec::GetIntraVertPred(MacroBlockH264* pMb, OverlayMem2Dv2* img, OverlayMem2Dv2* pred, int lumFlag)
{
	int i;
	MacroBlockH264* amb = pMb->_aboveMb;

	if (amb != NULL)
	{
		short**	img2D = img->Get2DSrcPtr();
		short**	pred2D = pred->Get2DSrcPtr();
		int	width = pred->GetWidth();
		int	height = pred->GetHeight();
		int offX = pred->GetOriginX();
		int offY = pred->GetOriginY();
		int byteLen = width * sizeof(short);
		/// The last row of the macroblock above.
		int iOffX, iOffY;
		if (lumFlag)
		{
			iOffX = amb->_offLumX;
			iOffY = amb->_offLumY + (img->GetHeight() - 1);
		}//end if lumFlag...
		else
		{
			iOffX = amb->_offChrX;
			iOffY = amb->_offChrY + (img->GetHeight() - 1);
		}//end else...
		short* ps = &(img2D[iOffY][iOffX]);
		for (i = 0; i < height; i++)	///< Rows.
			memcpy((void *)(&(pred2D[offY + i][offX])), (const void *)ps, byteLen);
		return(1);
	}//end if amb...

	/// Safety precaution not defined in H.264 standard. If the macroblock above
	/// is not available for prediction then this mode should never be used.
	pred->Fill(128);
	return(0);	///< Macroblock above does not exist.
}//end GetIntraVertPred.

/** Get Intra Horizontal prediction.
Predict from the input img and write it to the pred parameter. The dimensions
of the pred block define the size of the operation. If there are no valid
neighbours to make the prediction then use half the max value (128) as
the prediction. The H.264 std does not specify this last case for vert pred.
@param pMb				: Macroblock to transform.
@param img				: Img to predict from.
@param pred				: Overlay to write prediction result to.
@param lumFlag		: Is the prodiction for lum?
@return						: 1 = pred was made, 0 = no macroblocks available for pred.
*/
int H264v2Codec::GetIntraHorizPred(MacroBlockH264* pMb, OverlayMem2Dv2* img, OverlayMem2Dv2* pred, int lumFlag)
{
	int i, j;
	MacroBlockH264* lmb = pMb->_leftMb;

	if (lmb != NULL)
	{
		short**	img2D = img->Get2DSrcPtr();
		short**	pred2D = pred->Get2DSrcPtr();
		int	width = pred->GetWidth();
		int	height = pred->GetHeight();
		int offX = pred->GetOriginX();
		int offY = pred->GetOriginY();
		int byteLen = width * sizeof(short);
		/// The last col of the macroblock to the left.
		int iOffX, iOffY;
		if (lumFlag)
		{
			iOffX = lmb->_offLumX + (img->GetWidth() - 1);
			iOffY = lmb->_offLumY;
		}//end if lumFlag...
		else
		{
			iOffX = lmb->_offChrX + (img->GetWidth() - 1);
			iOffY = lmb->_offChrY;
		}//end else...
		for (i = 0; i < height; i++)	///< Rows.
		{
			short* pd = &(pred2D[offY + i][offX]);
			short  x = img2D[iOffY + i][iOffX];
			for (j = 0; j < width; j++)
				*pd++ = x;
		}//end for i...
		return(1);
	}//end if lmb...

	/// Safety precaution not defined in H.264 standard. If the left macroblock
	/// is not available for prediction then this mode should never be used.
	pred->Fill(128);
	return(0);	///< Left macroblock does not exist.
}//end GetIntraHorizPred.

/** Get Intra Chr prediction.
Select the best prediction mode based on the min distortion with the input img. Predict
from the ref chr img and write it to the pred parameter. Used by the encoder. Return the
intra_8x8 prediction mode.
@param pMb				: Macroblock to predict.
@param cb					: Cb Chr input image.
@param cr					: Cr Chr input image.
@param refCb			: Reference chr Cb img to predict from.
@param refCr			: Reference chr Cr img to predict from.
@param predCb			: 8x8 Cb overlay to write result to.
@param predCr			: 8x8 Cr overlay to write result to.
@return						: Prediction mode.
*/
int H264v2Codec::GetIntra8x8ChrPredAndMode(MacroBlockH264* pMb, OverlayMem2Dv2* cb, OverlayMem2Dv2* cr,
	OverlayMem2Dv2* refCb, OverlayMem2Dv2* refCr,
	OverlayMem2Dv2* predCb, OverlayMem2Dv2* predCr)
{
	int i, j;
	int aCb, bCb, cCb, aCr, bCr, cCr;	///< Plane mode intermediates.
	int predCbDC[4] = { 0, 0, 0, 0 };	///< DC mode intermediates.
	int predCrDC[4] = { 0, 0, 0, 0 };
	int modeDist[4] = { 0, 0, 0, 0 }; ///< Accumulator for the distortion tests.

	int mode = MacroBlockH264::Intra_Chr_DC;

	short**	inCb2D = cb->Get2DSrcPtr();
	short**	inCr2D = cr->Get2DSrcPtr();
	int			iOffX = cb->GetOriginX();	///< Assume Cb and Cr have the same offsets.
	int			iOffY = cb->GetOriginY();
	short**	refCb2D = refCb->Get2DSrcPtr();
	short**	refCr2D = refCr->Get2DSrcPtr();
	int			rOffX = refCb->GetOriginX();
	int			rOffY = refCb->GetOriginY();
	short**	predCb2D = predCb->Get2DSrcPtr();
	short**	predCr2D = predCr->Get2DSrcPtr();
	int			pOffX = predCb->GetOriginX();
	int			pOffY = predCr->GetOriginY();

	/// Every mode has intermediate values that the predictions are based on and
	/// these must be prepared first. The values are further dependent on the 
	/// availability of the neighbourhood.
	bool all = (pMb->_aboveMb != NULL) && (pMb->_aboveLeftMb != NULL) && (pMb->_leftMb != NULL);
	bool aboveOnly = (pMb->_aboveMb != NULL) && (pMb->_leftMb == NULL);	///< Don't care about above left.
	bool leftOnly = (pMb->_aboveMb == NULL) && (pMb->_leftMb != NULL);	///< Don't care about above left.
	bool aboveAndLeft = (pMb->_aboveMb != NULL) && (pMb->_leftMb != NULL);	///< Don't care about above left.

	/// The selection process uses a patial sum of the distortion method where a grid of sampling points
	/// is tested with new sets of points at each iteration. If there is no change in the winning mode
	/// after the iteration then that mode is selected.
	int pos = 0;	///< Position in the test sample point array. (Accumulates after each iteration.)
	int noChange = 0;

	if (all)	///< Every mode is a candidate.
	{
		///------------------- DC mode preparation -----------------------------------------
		for (i = 0; i < 4; i++)	///< Accumulate sums.
		{
			predCbDC[0] += ((int)refCb2D[rOffY - 1][rOffX + i] + (int)refCb2D[rOffY + i][rOffX - 1]);
			predCbDC[1] += (int)refCb2D[rOffY - 1][rOffX + 4 + i];
			predCbDC[2] += (int)refCb2D[rOffY + 4 + i][rOffX - 1];
			predCbDC[3] += ((int)refCb2D[rOffY - 1][rOffX + 4 + i] + (int)refCb2D[rOffY + 4 + i][rOffX - 1]);

			predCrDC[0] += ((int)refCr2D[rOffY - 1][rOffX + i] + (int)refCr2D[rOffY + i][rOffX - 1]);
			predCrDC[1] += (int)refCr2D[rOffY - 1][rOffX + 4 + i];
			predCrDC[2] += (int)refCr2D[rOffY + 4 + i][rOffX - 1];
			predCrDC[3] += ((int)refCr2D[rOffY - 1][rOffX + 4 + i] + (int)refCr2D[rOffY + 4 + i][rOffX - 1]);
		}//end for i...

	/// Calculate average values.
		predCbDC[0] = (predCbDC[0] + 4) >> 3;
		predCbDC[1] = (predCbDC[1] + 2) >> 2;
		predCbDC[2] = (predCbDC[2] + 2) >> 2;
		predCbDC[3] = (predCbDC[3] + 4) >> 3;

		predCrDC[0] = (predCrDC[0] + 4) >> 3;
		predCrDC[1] = (predCrDC[1] + 2) >> 2;
		predCrDC[2] = (predCrDC[2] + 2) >> 2;
		predCrDC[3] = (predCrDC[3] + 4) >> 3;

		///------------------- Plane mode preparation --------------------------------------
		int		HCb = 0;
		int		VCb = 0;
		int		HCr = 0;
		int		VCr = 0;
		for (i = 0; i < 4; i++)
		{
			HCb += (i + 1)*((int)refCb2D[rOffY - 1][rOffX + 4 + i] - (int)refCb2D[rOffY - 1][rOffX + 2 - i]);
			VCb += (i + 1)*((int)refCb2D[rOffY + 4 + i][rOffX - 1] - (int)refCb2D[rOffY + 2 - i][rOffX - 1]);
			HCr += (i + 1)*((int)refCr2D[rOffY - 1][rOffX + 4 + i] - (int)refCr2D[rOffY - 1][rOffX + 2 - i]);
			VCr += (i + 1)*((int)refCr2D[rOffY + 4 + i][rOffX - 1] - (int)refCr2D[rOffY + 2 - i][rOffX - 1]);
		}//end for i...
		bCb = (34 * HCb + 32) >> 6;
		cCb = (34 * VCb + 32) >> 6;
		aCb = ((int)refCb2D[rOffY + 7][rOffX - 1] + (int)refCb2D[rOffY - 1][rOffX + 7]) << 4;
		bCr = (34 * HCr + 32) >> 6;
		cCr = (34 * VCr + 32) >> 6;
		aCr = ((int)refCr2D[rOffY + 7][rOffX - 1] + (int)refCr2D[rOffY - 1][rOffX + 7]) << 4;

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_8x8_PATH_LENGTH; pos++)
		{
			int x = testPntPath8xy[pos][0];
			int y = testPntPath8xy[pos][1];

			/// Accumulate mode distortions here for these 4 points.
				  /// DC mode.
			int quadrant = 0;
			if ((y < 4) && (x > 3))
				quadrant = 1;
			else if (y > 3)
			{
				if (x < 4)
					quadrant = 2;
				else
					quadrant = 3;
			}//end else if y...
			modeDist[MacroBlockH264::Intra_Chr_DC] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], predCbDC[quadrant])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], predCrDC[quadrant])));
			/// Horiz mode.
			modeDist[MacroBlockH264::Intra_Chr_Horiz] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], (int)refCb2D[rOffY + y][rOffX - 1])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], (int)refCr2D[rOffY + y][rOffX - 1])));
			/// Vertical mode.
			modeDist[MacroBlockH264::Intra_Chr_Vert] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], (int)refCb2D[rOffY - 1][rOffX + x])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], (int)refCr2D[rOffY - 1][rOffX + x])));
			/// Plane mode.
			modeDist[MacroBlockH264::Intra_Chr_Plane] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], H264V2_CLIP255((aCb + bCb*(x - 3) + cCb*(y - 3) + 16) >> 5))) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], H264V2_CLIP255((aCr + bCr*(x - 3) + cCr*(y - 3) + 16) >> 5))));
		}//end for pos...
		mode = MacroBlockH264::Intra_Chr_DC;	///< MacroBlockH264::Intra_Chr_DC = 0.
		for (i = MacroBlockH264::Intra_Chr_Horiz; i < 4; i++)	///< Total of 4 modes.
		{
			if (modeDist[i] < modeDist[mode])
				mode = i;
		}//end for i...

	}//end if all...
	else if (aboveAndLeft)
	{
		///------------------- DC mode preparation -----------------------------------------
		for (i = 0; i < 4; i++)	///< Accumulate sums.
		{
			predCbDC[0] += ((int)refCb2D[rOffY - 1][rOffX + i] + (int)refCb2D[rOffY + i][rOffX - 1]);
			predCbDC[1] += (int)refCb2D[rOffY - 1][rOffX + 4 + i];
			predCbDC[2] += (int)refCb2D[rOffY + 4 + i][rOffX - 1];
			predCbDC[3] += ((int)refCb2D[rOffY - 1][rOffX + 4 + i] + (int)refCb2D[rOffY + 4 + i][rOffX - 1]);

			predCrDC[0] += ((int)refCr2D[rOffY - 1][rOffX + i] + (int)refCr2D[rOffY + i][rOffX - 1]);
			predCrDC[1] += (int)refCr2D[rOffY - 1][rOffX + 4 + i];
			predCrDC[2] += (int)refCr2D[rOffY + 4 + i][rOffX - 1];
			predCrDC[3] += ((int)refCr2D[rOffY - 1][rOffX + 4 + i] + (int)refCr2D[rOffY + 4 + i][rOffX - 1]);
		}//end for i...

	/// Calculate average values.
		predCbDC[0] = (predCbDC[0] + 4) >> 3;
		predCbDC[1] = (predCbDC[1] + 2) >> 2;
		predCbDC[2] = (predCbDC[2] + 2) >> 2;
		predCbDC[3] = (predCbDC[3] + 4) >> 3;

		predCrDC[0] = (predCrDC[0] + 4) >> 3;
		predCrDC[1] = (predCrDC[1] + 2) >> 2;
		predCrDC[2] = (predCrDC[2] + 2) >> 2;
		predCrDC[3] = (predCrDC[3] + 4) >> 3;

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_8x8_PATH_LENGTH; pos++)
		{
			int x = testPntPath8xy[pos][0];
			int y = testPntPath8xy[pos][1];

			/// Accumulate mode distortions here for these 4 points.
				  /// DC mode.
			int quadrant = 0;
			if ((y < 4) && (x > 3))
				quadrant = 1;
			else if (y > 3)
			{
				if (x < 4)
					quadrant = 2;
				else
					quadrant = 3;
			}//end else if y...
			modeDist[MacroBlockH264::Intra_Chr_DC] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], predCbDC[quadrant])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], predCrDC[quadrant])));
			/// Horiz mode.
			modeDist[MacroBlockH264::Intra_Chr_Horiz] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], (int)refCb2D[rOffY + y][rOffX - 1])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], (int)refCr2D[rOffY + y][rOffX - 1])));
			/// Vertical mode.
			modeDist[MacroBlockH264::Intra_Chr_Vert] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], (int)refCb2D[rOffY - 1][rOffX + x])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], (int)refCr2D[rOffY - 1][rOffX + x])));
		}//end for pos...

		mode = MacroBlockH264::Intra_Chr_DC;	///< MacroBlockH264::Intra_Chr_DC = 0.
		for (i = MacroBlockH264::Intra_Chr_Horiz; i < 3; i++)	///< Total of 4 modes.
		{
			if (modeDist[i] < modeDist[mode])
				mode = i;
		}//end for i...

	}//end else if aboveAndLeft...
	else if (leftOnly)
	{
		///------------------- DC mode preparation -----------------------------------------
		for (i = 0; i < 4; i++)	///< Accumulate sums.
		{
			predCbDC[0] += (int)refCb2D[rOffY + i][rOffX - 1];
			predCbDC[2] += (int)refCb2D[rOffY + 4 + i][rOffX - 1];

			predCrDC[0] += (int)refCr2D[rOffY + i][rOffX - 1];
			predCrDC[2] += (int)refCr2D[rOffY + 4 + i][rOffX - 1];
		}//end for i...
		/// Calculate average values.
		predCbDC[0] = (predCbDC[0] + 2) >> 2;
		predCbDC[1] = predCbDC[0];
		predCbDC[2] = (predCbDC[2] + 2) >> 2;
		predCbDC[3] = predCbDC[2];

		predCrDC[0] = (predCrDC[0] + 2) >> 2;
		predCrDC[1] = predCrDC[0];
		predCrDC[2] = (predCrDC[2] + 2) >> 2;
		predCrDC[3] = predCrDC[2];

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_8x8_PATH_LENGTH; pos++)
		{
			int x = testPntPath8xy[pos][0];
			int y = testPntPath8xy[pos][1];

			/// Accumulate mode distortions here for these 4 points.
				  /// DC mode.
			int partition = 0;
			if (y > 3)
				partition = 2;
			modeDist[MacroBlockH264::Intra_Chr_DC] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], predCbDC[partition])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], predCrDC[partition])));
			/// Horiz mode.
			modeDist[MacroBlockH264::Intra_Chr_Horiz] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], (int)refCb2D[rOffY + y][rOffX - 1])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], (int)refCr2D[rOffY + y][rOffX - 1])));
		}//end for pos...

		mode = MacroBlockH264::Intra_Chr_DC;	///< MacroBlockH264::Intra_Chr_DC = 0.
		if (modeDist[MacroBlockH264::Intra_Chr_Horiz] < modeDist[MacroBlockH264::Intra_Chr_DC])
			mode = MacroBlockH264::Intra_Chr_Horiz;

	}//end else if leftOnly...
	else if (aboveOnly)
	{
		///------------------- DC mode preparation -----------------------------------------
		for (i = 0; i < 4; i++)	///< Accumulate sums.
		{
			predCbDC[0] += (int)refCb2D[rOffY - 1][rOffX + i];
			predCbDC[1] += (int)refCb2D[rOffY - 1][rOffX + 4 + i];

			predCrDC[0] += (int)refCr2D[rOffY - 1][rOffX + i];
			predCrDC[1] += (int)refCr2D[rOffY - 1][rOffX + 4 + i];
		}//end for i...
		/// Calculate average values.
		predCbDC[0] = (predCbDC[0] + 2) >> 2;
		predCbDC[2] = predCbDC[0];
		predCbDC[1] = (predCbDC[1] + 2) >> 2;
		predCbDC[3] = predCbDC[1];

		predCrDC[0] = (predCrDC[0] + 2) >> 2;
		predCrDC[2] = predCrDC[0];
		predCrDC[1] = (predCrDC[1] + 2) >> 2;
		predCrDC[3] = predCrDC[1];

		///------------------- Test for min ------------------------------------------------
		for (pos = 0; pos < H264V2_8x8_PATH_LENGTH; pos++)
		{
			int x = testPntPath8xy[pos][0];
			int y = testPntPath8xy[pos][1];

			/// Accumulate mode distortions here for these 4 points.
				  /// DC mode.
			int partition = 0;
			if (x > 3)
				partition = 1;
			modeDist[MacroBlockH264::Intra_Chr_DC] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], predCbDC[partition])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], predCrDC[partition])));
			/// Vertical mode.
			modeDist[MacroBlockH264::Intra_Chr_Vert] += ((DISTORTIONSQRDIFF((int)inCb2D[iOffY + y][iOffX + x], (int)refCb2D[rOffY - 1][rOffX + x])) +
				(DISTORTIONSQRDIFF((int)inCr2D[iOffY + y][iOffX + x], (int)refCr2D[rOffY - 1][rOffX + x])));

		}//end for pos...
		mode = MacroBlockH264::Intra_Chr_DC;	///< MacroBlockH264::Intra_Chr_DC = 0.
		if (modeDist[MacroBlockH264::Intra_Chr_Vert] < modeDist[MacroBlockH264::Intra_Chr_DC])
			mode = MacroBlockH264::Intra_Chr_Vert;

	}//end else if aboveOnly...
	else
	{
		/// Use the DC default mode when there are no valid neighbours and use half the 
		/// max Chr value (128) as the prediction. This is typically the top left corner
		/// of the slice.
		predCb->Fill(128);
		predCr->Fill(128);
		return(mode);	///< Early exit.
	}//end else...

	/// Fill the prediction block with the lowest distortion mode prediction.
	switch (mode)
	{
	case MacroBlockH264::Intra_Chr_DC:
	{
		for (i = 0; i < 4; i++)
			for (j = 0; j < 4; j++)
			{
				predCb2D[pOffY + i][pOffX + j] = predCbDC[0];
				predCb2D[pOffY + i][pOffX + 4 + j] = predCbDC[1];
				predCb2D[pOffY + 4 + i][pOffX + j] = predCbDC[2];
				predCb2D[pOffY + 4 + i][pOffX + 4 + j] = predCbDC[3];

				predCr2D[pOffY + i][pOffX + j] = predCrDC[0];
				predCr2D[pOffY + i][pOffX + 4 + j] = predCrDC[1];
				predCr2D[pOffY + 4 + i][pOffX + j] = predCrDC[2];
				predCr2D[pOffY + 4 + i][pOffX + 4 + j] = predCrDC[3];
			}//end for i & j...
	}//end DC block...
	break;
	case MacroBlockH264::Intra_Chr_Horiz:
	{
		for (i = 0; i < 8; i++)
			for (j = 0; j < 8; j++)
			{
				predCb2D[pOffY + i][pOffX + j] = refCb2D[rOffY + i][rOffX - 1];
				predCr2D[pOffY + i][pOffX + j] = refCr2D[rOffY + i][rOffX - 1];
			}//end i & j...
	}//end Horiz block...
	break;
	case MacroBlockH264::Intra_Chr_Vert:
	{
		for (i = 0; i < 8; i++)
			for (j = 0; j < 8; j++)
			{
				predCb2D[pOffY + i][pOffX + j] = refCb2D[rOffY - 1][rOffX + j];
				predCr2D[pOffY + i][pOffX + j] = refCr2D[rOffY - 1][rOffX + j];
			}//end for i & j...
	}//end Vert block...
	break;
	case MacroBlockH264::Intra_Chr_Plane:
	{
		for (i = 0; i < 8; i++)
			for (j = 0; j < 8; j++)
			{
				predCb2D[pOffY + i][pOffX + j] = H264V2_CLIP255((aCb + bCb*(j - 3) + cCb*(i - 3) + 16) >> 5);
				predCr2D[pOffY + i][pOffX + j] = H264V2_CLIP255((aCr + bCr*(j - 3) + cCr*(i - 3) + 16) >> 5);
			}//end for i & j...
	}//end Plane block...
	break;
	}//end switch mode...

	return(mode);
}//end GetIntra8x8ChrPredAndMode.

/** Get Intra Chr DC prediction.
Predict from the parameter Chr img and write it to the pred parameter. If
there are no valid neighbours to make the prediction then use the
mid chr value (128) as the prediction. The H.264 std does not specify
this last case for DC pred.
@param pMb				: Macroblock to transform.
@param chr				: Chr img to predict from.
@param pred				: 8x8 overlay to write result to.
@return						: none
*/
void H264v2Codec::GetIntra8x8ChrDCPred(MacroBlockH264* pMb, OverlayMem2Dv2* chr, OverlayMem2Dv2* pred)
{
	int offX, offY, i;
	int predValue[4];
	int dirCnt = 0;
	int sum[4] = { 0, 0, 0, 0 };

	short** img = chr->Get2DSrcPtr();

	if (pMb->_aboveMb != NULL)
	{
		dirCnt++;
		/// The last row of the macroblock above.
		offX = pMb->_aboveMb->_offChrX;
		offY = pMb->_aboveMb->_offChrY + 7;
		for (i = 0; i < 4; i++)
		{
			sum[0] += img[offY][offX + i];
			sum[1] += img[offY][offX + 4 + i];
		}//end for i...
	}//end if _aboveMb...

	if (pMb->_leftMb != NULL)
	{
		dirCnt++;
		/// The last col of the macroblock to the left.
		offX = pMb->_leftMb->_offChrX + 7;
		offY = pMb->_leftMb->_offChrY;
		for (i = 0; i < 4; i++)
		{
			sum[2] += img[offY + i][offX];
			sum[3] += img[offY + 4 + i][offX];
		}//end for i...
	}//end if _leftMb...

	/// Set the accumulated DC prediction values.
	if (dirCnt == 2)
	{
		predValue[0] = (sum[0] + sum[2] + 4) >> 3;
		predValue[1] = (sum[1] + 2) >> 2;
		predValue[2] = (sum[3] + 2) >> 2;
		predValue[3] = (sum[1] + sum[3] + 4) >> 3;
	}//end if dirCnt...
	else if (dirCnt == 1)
	{
		if (pMb->_leftMb == NULL)
		{
			predValue[0] = (sum[0] + 2) >> 2;
			predValue[1] = (sum[1] + 2) >> 2;
			predValue[2] = (sum[0] + 2) >> 2;
			predValue[3] = (sum[1] + 2) >> 2;
		}//end if !leftMb...
		else
		{
			predValue[0] = (sum[2] + 2) >> 2;
			predValue[1] = (sum[2] + 2) >> 2;
			predValue[2] = (sum[3] + 2) >> 2;
			predValue[3] = (sum[3] + 2) >> 2;
		}//end else...
	}//end else if dirCnt...
	else
	{
		/// Mid Chr value.
		predValue[0] = 128;
		predValue[1] = 128;
		predValue[2] = 128;
		predValue[3] = 128;
	}//end else...

	int xBase = pred->GetOriginX();
	int yBase = pred->GetOriginY();
	pred->SetOverlayDim(4, 4);
	for (int y = 0, i = 0; y < 2; y++)
		for (int x = 0; x < 2; x++, i++)
		{
			pred->SetOrigin(xBase + x * 4, yBase + y * 4);
			pred->Fill(predValue[i]);
		}//end for y & x...

	/// Put the block back into shape.
	pred->SetOverlayDim(8, 8);
	pred->SetOrigin(xBase, yBase);

}//end GetIntra8x8ChrDCPred.

/** Get Intra Chr Plane prediction.
Predict from the parameter Chr img and write it to the pred parameter. This
implementation if for YCbCr 4:2:0 chroma_format_idc = 1 only. If there are
no valid neighbours to make the prediction then use the mid chr value (128)
as the prediction. The H.264 std does not specify this last case for DC pred.
@param pMb				: Macroblock to predict.
@param chr				: Chr img to predict from.
@param pred				: 8x8 overlay to write result to.
@return						: 1 = pred was made, 0 = no macroblocks available for pred.
*/
int H264v2Codec::GetIntra8x8ChrPlanePred(MacroBlockH264* pMb, OverlayMem2Dv2* chr, OverlayMem2Dv2* pred)
{
	int i, j;

	if ((pMb->_aboveMb != NULL) && (pMb->_aboveLeftMb != NULL) && (pMb->_leftMb != NULL))
	{
		short**	img2D = chr->Get2DSrcPtr();
		int			iOffX = chr->GetOriginX();	///< Top left of img2D pMb location.
		int			iOffY = chr->GetOriginY();
		short**	pred2D = pred->Get2DSrcPtr();
		int			pOffX = pred->GetOriginX();
		int			pOffY = pred->GetOriginY();

		int		H = 0;
		int		V = 0;
		for (i = 0; i < 4; i++)
		{
			H += (i + 1)*((int)img2D[iOffY - 1][iOffX + 4 + i] - (int)img2D[iOffY - 1][iOffX + 2 - i]);
			V += (i + 1)*((int)img2D[iOffY + 4 + i][iOffX - 1] - (int)img2D[iOffY + 2 - i][iOffX - 1]);
		}//end for i...
		int b = (34 * H + 32) >> 6;
		int c = (34 * V + 32) >> 6;
		int a = ((int)img2D[iOffY + 7][iOffX - 1] + (int)img2D[iOffY - 1][iOffX + 7]) << 4;

		for (i = 0; i < 8; i++)
			for (j = 0; j < 8; j++)
			{
				short x = (short)((a + b*(j - 3) + c*(i - 3) + 16) >> 5);
				pred2D[pOffY + i][pOffX + j] = (short)(H264V2_CLIP255(x));
			}//end for i & j...

		return(1);	///< Successful prediction.
	}//end if _aboveMb...

	/// Safety precaution not defined in H.264 standard. If the macroblock above, 
	/// above left and left are not available for prediction then this mode should 
	/// never be used.
	pred->Fill(128);
	return(0);	///< Prediction macroblocks do not exist.
}//end GetIntra8x8ChrPlanePred.

double H264v2Codec::EuclidianDistance(H264V2_COORD from, H264V2_COORD to)
{
  return(sqrt(((from.x - to.x)*(from.x - to.x)) + ((from.y - to.y)*(from.y - to.y))));
}//end EuclidianDistance.

/** Find a normalisation constant.
Use the largest euclidian distance from the focal point to corners
of the image as the norm constant.
@param  fpt : Focal point macroblock.
@return     : Normalisation constant.
*/
double H264v2Codec::NormalisationConstant(H264V2_COORD fpt)
{
  int bWidth = _width / 16;
  int bHeight = _height / 16;

  double norm = EuclidianDistance(fpt, H264V2_COORD{ 0, 0 });  ///< Default with upper left.

  double toUpperRight = EuclidianDistance(fpt, H264V2_COORD{ (bWidth - 1), 0 });
  if (toUpperRight > norm)
    norm = toUpperRight;

  double toLowerLeft = EuclidianDistance(fpt, H264V2_COORD{ 0, (bHeight - 1) });
  if (toLowerLeft > norm)
    norm = toLowerLeft;

  double toLowerRight = EuclidianDistance(fpt, H264V2_COORD{ (bWidth - 1), (bHeight - 1) });
  if (toLowerRight > norm)
    norm = toLowerRight;

  return(norm);
}//end NormalisationConstant.

 /** Find ROI map decay for the given position.
 The increasing distance from the focal point produces a decreasing decay value.
 @param  pt    : Current point
 @param  fpt   : Focal point
 @param  norm  : Normalisation constant
 @return       : ROI Map multiplier
 */
double H264v2Codec::DecayFunction(H264V2_COORD pt, H264V2_COORD fpt, double norm)
{
  double x = 1.0; ///< Default.

  double euclid = EuclidianDistance(fpt, pt) / norm;

  /// x = 1.0 - euclid;         ///< Linear y=1.x.
  /// x = cos(euclid*1.570796); ///< Cosine y=cos(x*pi/2).
  x = 1.0 - (euclid*euclid); ///< Parabola y=-x^2+1.

  /// Bounded in range [0.0...1.0]
  if (x < 0.0) x = 0.0;
  else if (x > 1.0) x = 1.0;

  /// Shift and scale function.
  //const double minMultiplier = 0.1;
  //x = (minMultiplier + ((1.0 - minMultiplier)*x));

  return(x);
}//end DecayFunction.

/*
-----------------------------------------------------------------------
  Private nested classes.
-----------------------------------------------------------------------
*/
/** Encode the image data for Intra IDR pictures.
Process all slices and their macroblocks in order. The reference is always
written into for this implementation regardless of the state of the writeRef
parameter. It is required for for later macroblock prediction. The macroblock
objs are prepared for coding onto the bit stream. The inclusion of the allowed
bits param provides for scope to use bit allocation procedures. Ver. 1 limits
the encoding to a single slice with no partitioning.
@param allowedBits	: Total remaining bits to target.
@param bitsUsed			: Num of bits used for this encoding (return value).
@param writeRef			: Ignored.
@return							: 1 = success, 0 = error.
*/
int H264v2Codec::IntraImgPlaneEncoderImplStdVer1::Encode(int allowedBits, int* bitsUsed, int writeRef)
{
	int mb;
	int len = _codec->_mbLength;

	/// Set up the input and ref image mem overlays.
	_codec->_Lum->SetOverlayDim(4, 4);
	_codec->_Cb->SetOverlayDim(4, 4);
	_codec->_Cr->SetOverlayDim(4, 4);
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_16x16->SetOrigin(0, 0);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_0->SetOrigin(0, 0);
	_codec->_8x8_1->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOrigin(0, 0);

	/// All integer transforms are Intra in this method.
	_codec->_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	_codec->_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TChr->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	/// By default the DC transforms were set in the TransformOnly mode in the Open() method.
	_codec->_pFDC4x4T->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	_codec->_pFDC2x2T->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);

	/// Whip through each macroblock in the slice and encode. The stream writing of
	/// the macroblock is seperate to allow further decision making later.
	for (mb = 0; mb < len; mb++)
	{
#ifdef H264V2_DUMP_MB_RD_DATA

		if ((mb > 66) && (_codec->_mbRDTablePos < 65)) ///< Collect data for 64 mbs starting in the 3rd mb row of the image.
		{
			/// Write the mb number in row 0.
			_codec->_mbRDTable.WriteItem(_codec->_mbRDTablePos, 0, mb);

			int qp;
			for (qp = 1; qp < _codec->_mbRDTableLen; qp++)
			{
				_codec->_pMb[mb]._mbQP = qp;
				_codec->ProcessIntraMbImplStd(&(_codec->_pMb[mb]), 2);  ///< D only.

				_codec->_mbRDTable.WriteItem(_codec->_mbRDTablePos, qp, _codec->_pMb[mb]._distortion[qp]);
			}//end for qp...

			_codec->_mbRDTablePos++;  ///< Represents columns. 
		}//end if mb...

#endif /// H264V2_DUMP_MB_RD_DATA

		_codec->_pMb[mb]._mbQP = _codec->_slice._qp;
		_codec->ProcessIntraMbImplStd(&(_codec->_pMb[mb]), 0);
	}//end for mb...

	*bitsUsed = 0;
	return(1);
}//end IntraImgPlaneEncoderImplStdVer1::Encode.

/** Decode of the Intra macroblocks to the reference img.
The macroblock obj encodings must be fully defined before calling
this method.
@return	: 1 = success, 0 = error.
*/
int H264v2Codec::IntraImgPlaneDecoderImplStdVer1::Decode(void)
{
	int mb;
	int len = _codec->_mbLength;

	/// Set up the input and ref image mem overlays.
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOverlayDim(8, 8);

	/// Whip through each macroblock. Decode the extracted encodings. All modes
	/// and parameters have been extracted by the ReadMacroBlockLayer() method.
	for (mb = 0; mb < len; mb++)
	{
		/// Simplify the referencing to the current macroblock.
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);
		int lOffX = pMb->_offLumX;
		int lOffY = pMb->_offLumY;
		int cOffX = pMb->_offChrX;
		int cOffY = pMb->_offChrY;

		/// --------------------- Inverse Transform & Quantisation --------------------------
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			_codec->InverseTransAndQuantIntra16x16MBlk(pMb, 0);

		/// --------------------- Image Prediction and Storing -------------------------------------
		/// From the prediction mode settings, make the appropriate prediction macroblock and then
		/// add the inverse transformed and quantised values to it. Fill the image (difference) colour 
		/// components from all the non-DC 4x4 blks (i.e. Not blks = -1, 17, 18) of the macroblock blocks.

		/// Store blocks into ref img.
		MacroBlockH264::StoreBlks(pMb, _codec->_RefLum, lOffX, lOffY, _codec->_RefCb, _codec->_RefCr, cOffX, cOffY, 0);

		/// Predict the output from the previously decoded neighbour ref macroblocks.
		/// Lum.
		_codec->_RefLum->SetOverlayDim(16, 16);
		_codec->_RefLum->SetOrigin(lOffX, lOffY); ///< Align the Ref Lum img block with this macroblock.

		switch (pMb->_intra16x16PredMode)
		{
		case MacroBlockH264::Intra_16x16_Vert:
			_codec->GetIntraVertPred(pMb, _codec->_RefLum, _codec->_16x16, 1);
			break;
		case MacroBlockH264::Intra_16x16_Horiz:
			_codec->GetIntraHorizPred(pMb, _codec->_RefLum, _codec->_16x16, 1);
			break;
		case MacroBlockH264::Intra_16x16_DC:
			_codec->GetIntra16x16LumDCPred(pMb, _codec->_RefLum, _codec->_16x16);
			break;
		case MacroBlockH264::Intra_16x16_Plane:
			_codec->GetIntra16x16LumPlanePred(pMb, _codec->_RefLum, _codec->_16x16);
			break;
		}//end switch _intra16x16PredMode...

		_codec->_RefCb->SetOverlayDim(8, 8);
		_codec->_RefCr->SetOverlayDim(8, 8);
		_codec->_RefCb->SetOrigin(cOffX, cOffY);
		_codec->_RefCr->SetOrigin(cOffX, cOffY);

		switch (pMb->_intraChrPredMode)
		{
		case MacroBlockH264::Intra_Chr_DC:
			_codec->GetIntra8x8ChrDCPred(pMb, _codec->_RefCb, _codec->_8x8_0);
			_codec->GetIntra8x8ChrDCPred(pMb, _codec->_RefCr, _codec->_8x8_1);
			break;
		case MacroBlockH264::Intra_Chr_Horiz:
			_codec->GetIntraHorizPred(pMb, _codec->_RefCb, _codec->_8x8_0, 0);
			_codec->GetIntraHorizPred(pMb, _codec->_RefCr, _codec->_8x8_1, 0);
			break;
		case MacroBlockH264::Intra_Chr_Vert:
			_codec->GetIntraVertPred(pMb, _codec->_RefCb, _codec->_8x8_0, 0);
			_codec->GetIntraVertPred(pMb, _codec->_RefCr, _codec->_8x8_1, 0);
			break;
		case MacroBlockH264::Intra_Chr_Plane:
			_codec->GetIntra8x8ChrPlanePred(pMb, _codec->_RefCb, _codec->_8x8_0);
			_codec->GetIntra8x8ChrPlanePred(pMb, _codec->_RefCr, _codec->_8x8_1);
			break;
		}//end switch _intraChrPredMode...

		/// --------------------- Add the prediction -----------------------------------------------
		/// Lum.
		_codec->_RefLum->SetOverlayDim(16, 16);
		_codec->_RefLum->SetOrigin(lOffX, lOffY);           ///< Align the Ref Lum img block with this macroblock.
		_codec->_RefLum->AddWithClip255(*(_codec->_16x16));	///< Add pred to ref Lum and leave result in ref img.
		/// Cb.
		_codec->_RefCb->SetOverlayDim(8, 8);
		_codec->_RefCb->SetOrigin(cOffX, cOffY);
		_codec->_RefCb->AddWithClip255(*(_codec->_8x8_0));
		/// Cr.
		_codec->_RefCr->SetOverlayDim(8, 8);
		_codec->_RefCr->SetOrigin(cOffX, cOffY);
		_codec->_RefCr->AddWithClip255(*(_codec->_8x8_1));

	}//end for mb...

	return(1);
}//end IntraImgPlaneDecoderImplStdVer1::Decode.

/** Encode the image data for Intra pictures with adaptive PQ (minmax).
The encoder seeks to find the optimal quant value for each macroblock using a minmax algorithm with
the constraint of a max number of bits specified in the method parameter list by allowedBits.
@param allowedBits	: Total remaining bits to target.
@param bitsUsed			: Num of bits used for this encoding.
@param writeRef			: 1/0 = do/do not update ref.
@return							: 1 = success, 0 = error.
*/
int H264v2Codec::IntraImgPlaneEncoderImplMinMax::Encode(int allowedBits, int* bitsUsed, int writeRef)
{
	int len = _codec->_mbLength;
	int mb;
	/// Mark the macroblock from where min bit rate macroblocks must be encoded if the slice is to be truncated.
	int minMBIndex = len;
	/// Set a bound on the max quality by choosing a min qp value that the MinMax search is limited to.
	int qEnd = _codec->_minQPIntra;

	/// Set up the input and ref image mem overlays.
	_codec->_Lum->SetOverlayDim(4, 4);
	_codec->_Cb->SetOverlayDim(4, 4);
	_codec->_Cr->SetOverlayDim(4, 4);
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_16x16->SetOrigin(0, 0);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_0->SetOrigin(0, 0);
	_codec->_8x8_1->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOrigin(0, 0);

	/// All integer transforms are Intra in this method.
	_codec->_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	_codec->_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TChr->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	/// By default the DC transforms were set in the TransformOnly mode in the Open() method.
	_codec->_pFDC4x4T->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	_codec->_pFDC2x2T->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);

	/// Initialisation step: 
	///		lower(D,R) : QP = H264V2_MAX_QP.
	///		upper(D,R) : QP = 1.
	/// Note that the D values represent the worst (max) distortion of only one macroblock in the entire
	/// frame but the R values represent the accumulated (total) rate of all macroblocks.
	int	Dl = 0;
	int	Du = 0;
	int	Rl = 0;
	int	Ru = 0;
	int	R = 0;
	int	D = 0;
	int	Dmax = 0;
	int mbDmax = 0;
	int	invalidated = 0;
	int	iterations = 0;
	/*
	  /// --------------------- I-Frame Measurement Collection -----------------------------------------------------
	  /// --------------------------------------------------------------------------------------------------
	*/
	/// Test QP = H264V2_MAX_QP for all macroblocks and determine if this min possible rate is, at the very 
	/// least, less than the target rate. Damage control is required if not, where minimal encodings
	/// are attempted for the macroblocks. Otherwise, proceed with the initialisation.
	for (mb = 0; mb < len; mb++)
	{
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);

		/// The 1st macroblock has a delta offset from _slice._qp to get it to _mbQP = H264V2_MAX_QP.
		pMb->_mbQP = H264V2_MAX_QP;
		/// Include all macroblocks at the start.
		pMb->_include = 1;
		_pQl[mb] = H264V2_MAX_QP;
		_pQ[mb] = H264V2_MAX_QP;	///< Active quantiser vector is always the lower (max distortion) side.

		/// Accumulate the rate and find the largest (max) distortion of all macroblocks.
		Rl += _codec->ProcessIntraMbImplStd(pMb, 1);
		if (pMb->_distortion[H264V2_MAX_QP] > Dl)
		{
			Dl = pMb->_distortion[H264V2_MAX_QP];
			mbDmax = mb;	///< Mark the macroblock with the peak distortion.
		}//end if _distortion...

	}//end for mb...

  /// Set the limits on the number of optimisation iterations by a timer or iteration number. Take into
  /// account the time taken to get to this point from _startTimer and assume half required after exiting.
	int start = 0;
	int timeOffset = 0;
	int lclIterations = _codec->_intraIterations;
	int lclTimeLimit = _codec->_timeLimitMs;  ///< In ms. A zero indicates either no counter available or not set.
	if (lclTimeLimit)
	{
		start = (int)_codec->GetCounter();
		timeOffset = 2 * (start - _codec->_startTime);
	}//end if lclTimeLimit...

	  /// Test that there is room to adapt else do damage control.
	int bitCost = Rl;
	int oldBitCount = Rl;
	if (Rl <= allowedBits)
	{
		/// Create an epsilon at 0.4% of the available bits with a min limit of 16 bits.
		/// Adjust the available bits to be epsilon below allowedBits.
		int closeEnough = allowedBits / 250;	///< 0.4%
		if (closeEnough < 16)
			closeEnough = 16;
		int closeEnoughDist = 8;	///< Sqr error per macroblock.

	  /// Model based faster algorithm to improve on the bi-section method.

		/// Modify the target rate to a close offset on the low rate side.
		int bitTarget = allowedBits - closeEnough;

		/// The worst case at QP = H264V2_MAX_QP uses less bits than allowed so there is some
		/// room to manouver and we can proceed. The upper point is initially not calculated
		/// directly but will hold a valid value during the algorithm convergence after the
		/// first iteration.
		/// The upper point is initiallised to some wild guess.
		Du = 1;									///< Min (im)possible.
		Ru = allowedBits << 8;	///< Some very large number.

		/// Initialisation is complete and the adapting bi-section/power model algorithm starts 
		/// here. The stopping criteria are 1) a rate less than, but close to, the allowed bits
		/// or 2) small change in distortion when reducing the interval.
		int goingDown = 1;
		int done = 0;
		while (!done)
		{
			int prevDmax = Dmax;

			/// Make a prediction assuming a power law model.
			int model = 0;
			Dmax = _codec->FitDistPowerModel(Rl, Dl, Ru, Du, bitTarget);
			/// Only use this prediction if it is bounded by (Rl,Dl) and (Ru,Du).
			if ((Dmax < Du) || (Dmax > Dl))	///< Out of bound.
			{
				Dmax = _codec->FitDistLinearModel(Rl, Dl, Ru, Du, bitTarget);
				model = 1;
			}//end if linear...

			/// Encourage the descent direction by adding a decreasing factor to the
			/// predicted Dmax such that convergence to Dmax is from the (Rl,Dl) side.
			Dmax += abs(Dl - Dmax) / 4;

			if ((Dmax < Du) || (Dmax > Dl) || (Dmax == prevDmax))	///< Still out of bound.
			{
				Dmax = ((Du + Dl) + 1) >> 1;	///< Set the midpoint max distortion.
				model = 2;
			}//end if bisection...

			/// At each macroblock reduce the quant value until the distortion is lower
			/// than Dmax. pQ[] must always hold the lower rate (smaller valued) quant vector 
			/// as the previous best choice.
			R = 0;
			int firstMbChange = len;
			if (invalidated)	///< Frame encoded coeffs must be invalidated if _pQ[] does not represent the actual QPs used.
				firstMbChange = 0;

			for (mb = 0; mb < len; mb++)
			{
				MacroBlockH264* pMb = &(_codec->_pMb[mb]);

				/// If the previous neighbourhood has not changed and QP is the same as the last encoding of this blk
				/// then it does not have to be re-encoded assuming distortion < Dmax.
				if ((mb >= firstMbChange) || (_pQ[mb] != pMb->_mbEncQP) || (pMb->_distortion[_pQ[mb]] > Dmax))
				{
					/// Record the found QP where the macroblock dist is just below Dmax and accumulate the rate for this macroblock.
					pMb->_mbQP = _pQ[mb];
					if (_pQ[mb] > 24)  ///< Assume that below QP = 24 the prediction mode will be the same.
						R += _codec->ProcessIntraMbImplStd(pMb, 1, 0, Dmax, qEnd);
					else
						R += _codec->ProcessIntraMbImplStd(pMb, 1, 1, Dmax, qEnd);
					_pQ[mb] = pMb->_mbQP;

					if (mb < firstMbChange)
						firstMbChange = mb;
				}//end if mb...
				else
					R += pMb->_rate[_pQ[mb]];

				/// An accurate early exit strategy is not possible because the model prediction require two valid (Dmax,R) points 
				/// for the whole frame. 
			}//end for mb...

	//			for(mb = 0; mb < len; mb++)
	//			{
	//				MacroBlockH264* pMb = &(_codec->_pMb[mb]);
	//				_pQ[mb] = _codec->GetMbQPBelowDmaxVer2(*pMb, _pQ[mb], Dmax, &firstMbChange, qEnd, 1);
	//				R += pMb->_rate[_pQ[mb]];
	//      }//end for mb...

				/// Test the stopping criteria.
			int timeExceeded = 0;
			if (lclTimeLimit)
			{
				int timeSoFar = (int)(_codec->GetCounter()) - start;
				int avgTimePerIteration = timeSoFar / (1 + iterations);
				int timeLimit = lclTimeLimit - timeOffset - avgTimePerIteration;
				if (timeSoFar > timeLimit)
					timeExceeded = 1;
			}//end if lclTimeLimit...

			int rBndDiff = abs(Ru - Rl);			///< Optimal solution is rate bounded in a small enough range.
			int dDiff = abs(prevDmax - Dmax);	///< Has not changed by much from the previous iteration (converged).
			int rDiff = abs(bitTarget - R);		///< Close enough to the allowed bits.
			if ((rBndDiff < (4 * closeEnough)) ||
				((rDiff < closeEnough) || (/*(R <= allowedBits)&&*/(dDiff < closeEnoughDist))) ||
				(iterations > lclIterations) || timeExceeded)
			{
				/// The new point is the preferable point to choose but if the rate is not below allowedBits
				/// then the (Rl,Dl) point is selected. Use the invalidated signal to indicate if the mbs 
		/// must be re-processed.
				if (R > allowedBits)
				{
					/// Load the lower vector as it is always below the target rate.
					memcpy((void *)_pQ, (const void *)_pQl, len * sizeof(int));
					R = Rl;
					invalidated = 1;
				}//end if R...
				else
					invalidated = 0;

				/// Optimal solution is found.
				done = 1;
			}//end if rDiff...
			else
			{
				if (allowedBits > R)	///< Inner predicted point is now lower point.
				{
					Dl = Dmax;
					Rl = R;
					/// pQ[] is the lower vector. 
					memcpy((void *)_pQl, (const void *)_pQ, len * sizeof(int));
					invalidated = 0;
				}//end if allowedBits...
				else							///< Inner predicted point is now upper point.
				{
					Du = Dmax;
					Ru = R;
					/// pQ[] must be reset to lower vector to maintain a descent convergence.
					memcpy((void *)_pQ, (const void *)_pQl, len * sizeof(int));
					invalidated = 1;	///< pQ[] now does not represented the state of the last encoded frame.
				}//end else...

			}//end else...

			iterations++;
		}//end while !done...

		/// Set the macroblock QP values to their found states.
		if (invalidated)
		{
			for (mb = 0; mb < len; mb++)
				_codec->_pMb[mb]._mbQP = _pQ[mb];
		}//end if invalidated...

		/// All good.
		//sprintf(_codec->_errorInfo, "I-frame: Normal minmax path Allowed=%d Cost=%d Invalid=%d", allowedBits, R, invalidated);
		//sprintf(_codec->_errorInfo, "I-frame: Normal minmax");
		bitCost = R;

	}//end if Rl...
	else ///< For QP = H264V2_MAX_QP the max allowed bits was exceeded and damage control is required.
	{
		///======================================================================================

		/// Sub-optimal Steepest Ascent Algorithm
		bitCost = DamageControl(allowedBits, bitCost);

		if (bitCost >= allowedBits) ///< All failed.
		{
			minMBIndex = _codec->_lumWidth / 16; ///< Hope for the best.
			invalidated = 1;
			for (int mb = 0; mb < len; mb++)
				_codec->_pMb[mb]._mbQP = _codec->_pMb[mb]._mbEncQP;
		}//end if bitCost...

		/// Damage control path.
		//sprintf(_codec->_errorInfo, "I-frame: Damage control path Allowed=%d Cost=%d Iter=%d ListLen=%d ReorderCnt=%d", allowedBits, bitCost, iterations, listLen, cnt);
		//sprintf(_codec->_errorInfo, "I-frame: Damage control");

	}//end else...

	/// Apply the selected quant with the full feedback loop to all macroblocks.
	if (invalidated)
	{
		//  	int bitCount = 0;
		for (mb = 0; mb < len; mb++)
		{
			MacroBlockH264* pMb = &(_codec->_pMb[mb]);

			/// Encode the macroblocks until min null macroblocks.
			if (mb < minMBIndex)
				_codec->ProcessIntraMbImplStd(pMb, 0);
			//  			bitCount += _codec->ProcessIntraMbImplStd(pMb, 1);
			else
				_codec->ProcessIntraMbImplStdMin(pMb);
			//  			bitCount += _codec->ProcessIntraMbImplStdMin(pMb);

		}//end for mb...

  //    bitCost = bitCount;

	}//end if invalidated...

	*bitsUsed = 0;
	//  *bitsUsed = bitCost;
	return(1);
}//end IntraImgPlaneEncoderImplMinMax::Encode.

/** Damage control algorithm
At max QP for all Mbs the rate limit is exceeded and an alternative quantisation
approach is taken to use the available bits as best as possible. This implementation
zeros more and more of the DCT coeffs until it fits using a sub-optimal Steepest
Ascent Algorithm.
@param  allowedBits : Bits to use.
@param  currBitCost : Bits used up to this point.
@return             : The actual bits consumed.
*/
int H264v2Codec::IntraImgPlaneEncoderImplMinMax::DamageControl(int allowedBits, int currBitCost)
{
	int i, mb;
	int bitCost = currBitCost;
	int iterations = 0;
	int cnt;

	/// Initialise the ordered mb list.
	int len = _codec->_mbLength;
	int listLen = len;
	for (mb = 0; mb < len; mb++)
		_pMbList[mb] = mb;

	while ((bitCost >= allowedBits) && (listLen > 1))
	{
		int predR = bitCost;

		while ((listLen > 1) && (predR >= allowedBits))
		{
			/// Step 1: Order the mbs in increasing distortion.

			/// Exclude completed mbs from the old list.
			int currListLen = listLen;
			listLen = 0;
			for (i = 0; i < currListLen; i++)
			{
				mb = _pMbList[i];
				if (_codec->_pMb[mb]._mbEncQP < H264V2_I_MAX_EXT_QP)
					_pMbList[listLen++] = mb;
			}//end for mb...

			/// Bubble sort list in ascending order of distortion.
			cnt = len;
			while (cnt)
			{
				cnt = 0; ///< Count how many re-orderings occur.
				for (i = 1; i < listLen; i++)
				{
					MacroBlockH264* pMb1 = &(_codec->_pMb[_pMbList[i - 1]]);
					MacroBlockH264* pMb2 = &(_codec->_pMb[_pMbList[i]]);
					if (pMb2->_distortion[pMb2->_mbEncQP] < pMb1->_distortion[pMb1->_mbEncQP])
					{
						/// Swap places.
						int tmp = _pMbList[i - 1];
						_pMbList[i - 1] = _pMbList[i];
						_pMbList[i] = tmp;
						cnt++;
					}//end if _distortion...
				}//end for i...
			}//end while cnt...

			/// Step 2: Increase the distortion of mb at the head of the ordered list to just more than the
			/// next one in the list.
			int mb1 = _pMbList[0]; /// Head of ordered list.
			int mb2 = _pMbList[1]; /// Next in ordered list.
			int d1 = _codec->_pMb[mb1]._distortion[_codec->_pMb[mb1]._mbEncQP];
			int d2 = _codec->_pMb[mb2]._distortion[_codec->_pMb[mb2]._mbEncQP];

			/// For the unusual case where the listLen == 1, set them to the same.
			if (listLen == 1)
				d2 = d1;

			if ((d1 <= d2) && (_codec->_pMb[mb1]._mbEncQP < H264V2_I_MAX_EXT_QP))
			{
				MacroBlockH264* pMb = &(_codec->_pMb[mb1]);  ///< Simplify mb addressing.

				predR -= pMb->_rate[pMb->_mbEncQP]; ///< Remove old bits before processing.
				while ((pMb->_distortion[pMb->_mbEncQP] <= d2) && (pMb->_mbEncQP < H264V2_I_MAX_EXT_QP))
				{
					/// Select new QP.
					switch (pMb->_mbEncQP)
					{
					case 51: pMb->_mbQP = 59; break;
					case 59: pMb->_mbQP = 63; break;
					case 63: pMb->_mbQP = 66; break;
					case 66: pMb->_mbQP = 67; break;
					case 67: pMb->_mbQP = 68; break;
					case 68: pMb->_mbQP = 69; break;
					case 69: pMb->_mbQP = 77; break;
					case 77: pMb->_mbQP = 81; break;
					case 81: pMb->_mbQP = 84; break;
					case 84: pMb->_mbQP = 85; break;
					case 85: pMb->_mbQP = 86; break;
					default: pMb->_mbQP = 86; break;
					}///end switch _mbEncQP...

					_codec->ProcessIntraMbImplStd(pMb, 2, 1); ///< Distortion only and without pred mode selection.

				}//end while _distortion[]...

				/// Add back new bits.
				pMb->_rate[pMb->_mbEncQP] = _codec->MacroBlockLayerBitCounter(pMb);
				predR += pMb->_rate[pMb->_mbEncQP];

			}//end if d1...

		}//end while listLen...

		/// Step 3: Test the new solution.
		bitCost = 0;
		for (int mb = 0; mb < len; mb++)
		{
			MacroBlockH264* pMb = &(_codec->_pMb[mb]);

			pMb->_mbQP = pMb->_mbEncQP; ///< Restore the QP that the mb was coded with.
			bitCost += _codec->ProcessIntraMbImplStd(pMb, 1, 1);
		}//end for mb...

		iterations++;
	}//end while bitCost...

	return(bitCost);
}//end IntraImgPlaneEncoderImplMinMax::DamageControl.

/** Encode the image data for Intra IDR pictures with a mb Dmax limit.
Process all slices and their macroblocks in order. The QP value is decremented
until the mb Dmax is less than the codec _dMax parameter. The reference is always
written into for this implementation regardless of the state of the writeRef
parameter. It is required for for later macroblock prediction. The macroblock
objs are prepared for coding onto the bit stream. The inclusion of the allowed
bits param provides for scope to use bit allocation procedures. Ver. 1 limits
the encoding to a single slice with no partitioning.
@param allowedBits	: Total remaining bits to target.
@param bitsUsed			: Num of bits used for this encoding (return value).
@param writeRef			: Ignored.
@return							: 1 = success, 0 = error.
*/
int H264v2Codec::IntraImgPlaneEncoderImplDMax::Encode(int allowedBits, int* bitsUsed, int writeRef)
{
	int mb;
	int len = _codec->_mbLength;
	int dmax = _codec->_dMax;
	/// Set a bound on the max quality by choosing a min qp value that the Dxax search is limited to.
	int qEnd = _codec->_minQPIntra;
	int coeffBits = 0;

	if (_codec->_modeOfOperation == H264V2_DMAX)
		if ((_codec->_iPictureDMaxMultiplier != 0) || (_codec->_iPictureDMaxMultiplier != 0))
			/// Modify the Dmax value by the I-Picture multiplier and fraction.
			dmax = (dmax * _codec->_iPictureDMaxMultiplier) + ((dmax * _codec->_iPictureDMaxFraction) / 10);

	/// Set up the input and ref image mem overlays.
	_codec->_Lum->SetOverlayDim(4, 4);
	_codec->_Cb->SetOverlayDim(4, 4);
	_codec->_Cr->SetOverlayDim(4, 4);
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_16x16->SetOrigin(0, 0);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_0->SetOrigin(0, 0);
	_codec->_8x8_1->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOrigin(0, 0);

	/// All integer transforms are Intra in this method.
	_codec->_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	_codec->_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TChr->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	/// By default the DC transforms were set in the TransformOnly mode in the Open() method.
	_codec->_pFDC4x4T->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);
	_codec->_pFDC2x2T->SetParameter(IForwardTransform::INTRA_FLAG_ID, 1);

	/// Whip through each macroblock in the slice and encode. The stream writing of
	/// the macroblock is seperate to allow further decision making later.
  int accumulatedD = 0;
  for (mb = 0; mb < len; mb++)
	{
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);

		/// The 1st macroblock has a delta offset from _slice._qp to get it to _mbQP = H264V2_MAX_QP.
		pMb->_mbQP = H264V2_MAX_QP;
		/// Include all macroblocks at the start.
		pMb->_include = 1;
    /// Find QP where the mb Lum dist is just below Dmax for this mb. Count the bits for the coeffs only.
    if (_codec->_modeOfOperation == H264V2_MINAVG_RATECNT)
    {
      /// In this mode of operation the mb Dmax parameter is interpreted as Davg per mb.
      int davg = dmax;
      if (mb)  ///< All mbs except the 1st
      {
        /// Determine the avg per mb for the remaining mbs.
        davg = ((len*dmax) - accumulatedD) / (len - mb);
        if (davg <= 0) davg = dmax; ///< If all goes wrong then revert to dmax
      }//end if mb...

      coeffBits += _codec->ProcessIntraMbImplStd(pMb, 1, 0, davg, qEnd);
      accumulatedD += pMb->_distortion[pMb->_mbQP];
    }//end if H264V2_MINAVG_RATECNT...
    else if (_codec->_modeOfOperation == H264V2_MINMAX_RATECNT)
			coeffBits += _codec->ProcessIntraMbImplStd(pMb, 1, 0, dmax, qEnd);
		else
			_codec->ProcessIntraMbImplStd(pMb, 2, 0, dmax, qEnd);
	}//end for mb...

  /// Store the coeff bits per pel.
	_codec->_coeffBitsPerPel = (double)coeffBits / (double)(_codec->_lumWidth*_codec->_lumHeight);
  if (_codec->_modeOfOperation == H264V2_MINAVG_RATECNT)
    _codec->_frameDistortion = accumulatedD / len;  ///< Avg distortion per mb.
  else
    _codec->_frameDistortion = dmax;  ///< Max mb distortion for all mbs.

	*bitsUsed = 0;
	return(1);
}//end IntraImgPlaneEncoderImplDMax::Encode.

/** Get distortion from an r vs d power model.
@return	:	Distortion predicted
*/
int H264v2Codec::FitDistPowerModel(int rl, int dl, int ru, int du, int r)
{
	int d = dl;
	if ((dl != du) && (rl != ru))
	{
		/// Curve fit the points (rl,dl) and (ru,du) to a power function.
		double beta = -(log((double)rl / (double)ru)) / (log((double)dl / (double)du));
		double alpha = (double)rl / (pow((double)dl, -beta));
		/// Use the power function to predict where r will be. r = alpha.d^(-beta)
		d = (int)exp(-log((double)r / alpha) / beta);
	}//end if dl...

	return(d);
}//end FitDistPowerModel.

/** Get distortion from an r vs d linear model.
@return	:	Distortion predicted
*/
int H264v2Codec::FitDistLinearModel(int rl, int dl, int ru, int du, int r)
{
	int d = dl;
	if (rl != ru)
	{
		/// Curve fit the points (rl,dl) and (ru,du) to a linear function.
		d = (int)((double)((r - rl)*(dl - du)) / (double)(rl - ru)) + dl;
	}//end if dl...

	return(d);
}//end FitDistLinearModel.

/** Get the next quant QP value that has a distortion less than Dmax.
@param mb						: Macroblock to operate on.
@param atQP					: Current QP for this macroblk.
@param Dmax					: Max value to get below.
@param decQP				: Decrement=1/increment=0 QP from atQ for this macroblock.
@param changeMb			: Macroblock index from where the last change was made.
@param lowestQP			: Lowest allowed quant.
@param intra				: Is macroblock Intra.
@return							: QP value that has dist < Dmax for this macroblock.
*/
int H264v2Codec::GetMbQPBelowDmax(MacroBlockH264 &mb, int atQP, int Dmax, int decQP, int* changeMb, int lowestQP, bool intra)
{
	/// 1st iteration required to ensure sync with macroblock dependencies if
	/// it was invalidated or changed since last encoding.
	int i = atQP;
	int	di = mb._distortion[i];
	int mbIndex = mb._mbIndex;
	int lclChangeMb = *changeMb;

	/// Macroblocks are dependent on (predicted from) previous macroblocks in the slice. If
	/// this mb is located after the previously changed mb then its dependencies have been
	/// altered and it must be re-encoded.
	if (mbIndex >= lclChangeMb)
	{
		mb._mbQP = i;
		if (!intra)
			;
		else
			ProcessIntraMbImplStd(&mb, 1);

		di = mb._distortion[i];
	}//end if mbIndex...

	/// If an increasing QP is requested but di > Dmax then a decrease
	/// in mb QP is actually what is required.
	if (!decQP && (di > Dmax))
		decQP = 1;

	if (decQP)	///< Decreasing QP until di < Dmax.
	{
		/// Check if it is necessary to look further.
		if ((i > lowestQP) && (di > Dmax))
		{
			if (mbIndex < lclChangeMb)
				lclChangeMb = mbIndex;

			/// Iterate QP down from i. If i drops to lowestQ without di <= Dmax
			/// then leave as it is the best that can be done.
			while ((i > lowestQP) && (di > Dmax))
			{
				i -= MbStepSize[i];
				if (i < lowestQP)
					i = lowestQP;

				mb._mbQP = i;
				if (!intra)
					;
				else
					ProcessIntraMbImplStd(&mb, 1);
				di = mb._distortion[i];
			}//end while i...

		}//end if i...
	}//end if decQP...
	else		///< Increasing QP until di > Dmax and then decrease back to below Dmax.
	{
		/// Check if it is necessary to look further.
		if (i < H264V2_MAX_QP)
		{
			if (mbIndex < lclChangeMb)
				lclChangeMb = mbIndex;

			/// Iterate QP up from i until di > Dmax or i = H264V2_MAX_QP (51).
//			int previ = i;
			while ((i < H264V2_MAX_QP) && (di <= Dmax))
			{
				//				previ = i;
				i += MbStepSize[i];
				if (i > H264V2_MAX_QP)
					i = H264V2_MAX_QP;

				mb._mbQP = i;
				if (!intra)
					;
				else
					ProcessIntraMbImplStd(&mb, 1);
				di = mb._distortion[i];
			}//end while i...

			/// Compensate only if a solution was found otherwise QP = H264V2_MAX_QP is the
			/// best that can be done. It is expected to step back only once but if the step
			/// up process above was not implemented because di > Dmax then iterating down
			/// several times may be required.
			if (di > Dmax)
			{
				/// Iterate down from i. If i drops to lowestQ without di <= Dmax
				/// then leave as it is the best that can be done.
				while ((i > lowestQP) && (di > Dmax))
				{
					i -= MbStepSize[i];
					if (i < lowestQP)
						i = lowestQP;

					mb._mbQP = i;
					if (!intra)
						;
					else
						ProcessIntraMbImplStd(&mb, 1);
					di = mb._distortion[i];
				}//end while i...

			}//end if di...

		}//end if i...

	}//end else...

	/// Return QP value where dist just less than Dmax for this macroblock.
	*changeMb = lclChangeMb;
	return(i);
}//end GetMbQPBelowDmax.

/** Get the next quant value that has a distortion less than Dmax with decrement only.
@param mb						: Macroblock to operate on.
@param atQ					: Current q for this macroblk.
@param Dmax					: Max value to get below.
@param changeMb			: Macroblock index from where the last change was made.
@param lowestQ			: Lowest allowed quant.
@param intra				: Is macroblock Intra.
@return							: QP value that has dist < Dmax for this macroblock.
*/
int H264v2Codec::GetMbQPBelowDmaxVer2(MacroBlockH264 &mb, int atQ, int Dmax, int* changeMb, int lowestQ, bool intra)
{
	int i = atQ;
	int	di = mb._distortion[i];
	int mbIndex = mb._mbIndex;
	int lclChangeMb = *changeMb;

	/// The delta QP for a mb is constrained to {-26...25}. Therefore if
	/// the current QP value results in a dQP > 25 or < -26 then it must 
	/// be reprocessed before continuing regardless of the Dmax value. The
	/// i not equal to atQ signals that delta QP was out of range.
	int prevQP = GetPrevMbQP(&mb);
	int minQP = prevQP - 26;
	if (minQP < lowestQ) minQP = lowestQ;
	int maxQP = prevQP + 25;
	if (maxQP > H264V2_MAX_QP) maxQP = H264V2_MAX_QP;
	if (i < minQP) i = minQP;
	else if (i > maxQP) i = maxQP;

	/// Macroblocks are dependent on (predicted from) previous macroblocks in the slice. If
	/// this mb is located after the previously changed mb or delta QP was out of range then
  /// its dependencies have been altered and it must be re-encoded.
	if ((mbIndex >= lclChangeMb) || (i != atQ))
	{
		mb._mbQP = i;
		if (!intra)
		{
			/// For P mbs, if the previous encoding QP was the same then only the rate of the mb
			/// will change. This rate change is due to a change in the number of coeffs of neighbouring
			/// mbs and their changed QP values.
			if (mb._mbEncQP == i)
				mb._mb_qp_delta = GetDeltaQP(&mb);        ///< Process new delta QP.
			else
				ProcessInterMbImplStd(&mb, 0, 2);         ///< Distortion only.
		}//end if !intra...
		else
			ProcessIntraMbImplStd(&mb, 2, 0); ///< Distortion only.

		di = mb._distortion[i];
	}//end if mbIndex...

	/// Check if it is necessary to look further.
	if ((i > minQP) && (di > Dmax))
	{
		if (mbIndex < lclChangeMb)
			lclChangeMb = mbIndex;

		/// Iterate QP down from i. If i drops to minQP without di <= Dmax
		/// then leave, as it is the best that can be done.
		while ((i > minQP) && (di > Dmax))
		{
			i -= MbStepSize[i];
			if (i < minQP) i = minQP;

			mb._mbQP = i;
			if (!intra)
				ProcessInterMbImplStd(&mb, 0, 2);
			else
				ProcessIntraMbImplStd(&mb, 2, 1); ///< Distortion only without pred mode selection.

			di = mb._distortion[i];

		}//end while i...
	}//end if i...

  /// Set the new mb rate at this QP value.
	if (!intra)
	{
		int rate = 0;
		if (!mb._skip)
			rate = MacroBlockLayerBitCounter(&mb);  ///< Process mb for new neighbour coeffs values.
		mb._rate[mb._mbEncQP] = rate;             ///< Update this new rate.
	}//end if !intra...
	else
		mb._rate[mb._mbEncQP] = MacroBlockLayerBitCounter(&mb);

	/// Return QP value where dist just less than Dmax for this macroblock.
	*changeMb = lclChangeMb;
	return(i);
}//end GetMacroblkQuantBelowDmaxVer2.

/** Get the next quant value that has a distortion less than Dmax with decrement only.
This method includes extended QP values = {52..71}.
@param mb						: Macroblock to operate on.
@param atQ					: Current q for this macroblk.
@param Dmax					: Max value to get below.
@param changeMb			: Macroblock index from where the last change was made.
@param lowestQ			: Lowest allowed quant.
@param intra				: Is macroblock Intra.
@return							: QP value that has dist < Dmax for this macroblock.
*/
int H264v2Codec::GetMbQPBelowDmaxVer3(MacroBlockH264 &mb, int atQ, int Dmax, int* changeMb, int lowestQ, bool intra)
{
	int i = atQ;
	int	di = mb._distortion[i];
	int mbIndex = mb._mbIndex;
	int lclChangeMb = *changeMb;

	/// Macroblocks are dependent on (predicted from) previous macroblocks in the slice. If
	/// this mb is located after the previously changed mb then its dependencies have been
	/// altered and it must be re-encoded.
	if (mbIndex >= lclChangeMb)
	{
		mb._mbQP = i;
		if (!intra)
		{
			/// For P mbs, if the previous encoding QP was the same then only the rate of the mb
			/// will change. This rate change is due to a change in the number of coeffs of neighbouring
			/// mbs and their changed QP values.
			if (mb._mbEncQP == i)
			{
				if (mb._mbQP > H264V2_MAX_QP)              ///< For extended QP range, temporarily reset for processes below.
					mb._mbQP = H264V2_MAX_QP;

				int rate = 0;
				mb._mb_qp_delta = GetDeltaQP(&mb);        ///< Process new delta QP.
				if (!mb._skip)
					rate = MacroBlockLayerBitCounter(&mb);  ///< Process mb for new neighbour coeffs values.
				mb._rate[mb._mbEncQP] = rate;             ///< Update this new rate.

				mb._mbQP = i;                             ///< Put it back.
			}//end if _mbEncQP...
			else
			{
				ProcessInterMbImplStd(&mb, 0, 1);
			}//end else...

		}//end if !intra...
		else
		{
			ProcessIntraMbImplStd(&mb, 1);
		}//end else...

		di = mb._distortion[i];
	}//end if mbIndex...

  /// The delta QP for a mb is constrained to {-26...25}. Therefore if
  /// the current QP value results in a dQP > 25 or < -26 then it must 
  /// be reprocessed before continuing regardless of the Dmax value.
	int minQP = lowestQ;
	if (i <= H264V2_MAX_QP) ///< Only for the non-extended QP range.
	{
		int prevQP = GetPrevMbQP(&mb);
		minQP = prevQP - 26;
		if (minQP < lowestQ) minQP = lowestQ;
		int maxQP = prevQP + 25;
		if (maxQP > H264V2_MAX_QP) maxQP = H264V2_MAX_QP;
		if ((i < minQP) || (i > maxQP))
		{
			if (i < minQP) i = minQP;
			else i = maxQP;
			mb._mbQP = i;
			if (!intra)
				ProcessInterMbImplStd(&mb, 0, 1);
			else
				ProcessIntraMbImplStd(&mb, 1);

			di = mb._distortion[i];
		}//end if i...
	}//end if i...

	  /// Check if it is necessary to look further.
	if ((i > minQP) && (di > Dmax))
	{
		if (mbIndex < lclChangeMb)
			lclChangeMb = mbIndex;

		/// Iterate QP down from i. If i drops to minQP without di <= Dmax
		/// then leave, as it is the best that can be done.
		while ((i > minQP) && (di > Dmax))
		{
			i -= MbStepSize[i];
			if (i < minQP) i = minQP;

			mb._mbQP = i;
			if (!intra)
				ProcessInterMbImplStd(&mb, 0, 1);
			else
				ProcessIntraMbImplStd(&mb, 1);

			di = mb._distortion[i];

		}//end while i...
	}//end if i...

	/// Return QP value where dist just less than Dmax for this macroblock.
	*changeMb = lclChangeMb;
	return(i);
}//end GetMacroblkQuantBelowDmaxVer3.

/** Get the next quant QP value that has a distortion less than Dmax with approximations.
@param mb						: Macroblock to operate on.
@param atQP					: Current QP for this macroblk.
@param Dmax					: Max value to get below.
@param epsilon			: The Dmax approximation only has to be within epsilon of Dmax.
@param decQP				: Decrement=1/increment=0 QP from atQ for this macroblock.
@param changeMb			: Macroblock index from where the last change was made.
@param lowestQP			: Lowest allowed quant.
@param intra				: Is macroblock Intra.
@return							: QP value that has dist < Dmax for this macroblock.
*/
int H264v2Codec::GetMbQPBelowDmaxApprox(MacroBlockH264 &mb, int atQP, int Dmax, int epsilon, int decQP, int* changeMb, int lowestQP, bool intra)
{
	int i = atQP;
	int	di = mb._distortion[i];
	int mbIndex = mb._mbIndex;
	int lclChangeMb = *changeMb;

	/// Macroblocks are dependent on (predicted from) previous macroblocks in the slice. If
	/// this mb is located after the previously changed mb then its dependencies have been
	/// altered and it must be re-encoded. However, as it will be changed anyway, either up
	/// or down, a short cut approximation is to assume that the previous encoding at the
	/// current QP is approximately correct and the descent/ascent proceeds with these values.
	/// But the mb must be processed at least once before exiting to be valid.
	int atLeastOnce = 0;

	/// If an increasing QP is requested but di > Dmax then a decrease
	/// in mb QP is actually what is required.
	if (!decQP && (di > Dmax))
		decQP = 1;

	if (decQP)	///< Decreasing QP until di < Dmax.
	{
		/// Check if it is necessary to look further.
		if ((i > lowestQP) && (di > Dmax))
		{
			if (mbIndex < lclChangeMb)
				lclChangeMb = mbIndex;

			atLeastOnce = 1;	///< Signal that the mb is encoded.

			/// Iterate QP down from i. If i drops to lowestQ without di <= Dmax
			/// then leave as it is the best that can be done.
			while ((i > lowestQP) && (di > Dmax))
			{
				i -= MbStepSize[i];
				if (i < lowestQP)
					i = lowestQP;

				mb._mbQP = i;
				if (!intra)
					;
				else
					ProcessIntraMbImplStd(&mb, 1);
				di = mb._distortion[i];
			}//end while i...

		}//end if i...
	}//end if decQP...
	else		///< Increasing QP until di > Dmax and then decrease back to below Dmax.
	{
		/// Check if it is necessary to look further.
		if ((i < H264V2_MAX_QP) && ((Dmax - di) > epsilon))
		{
			if (mbIndex < lclChangeMb)
				lclChangeMb = mbIndex;

			atLeastOnce = 1;	///< Signal that the mb is encoded.

			/// Iterate QP up from i until di > Dmax or i = H264V2_MAX_QP (51).
			while ((i < H264V2_MAX_QP) && (di <= Dmax))
			{
				i += MbStepSize[i];
				if (i > H264V2_MAX_QP)
					i = H264V2_MAX_QP;

				mb._mbQP = i;
				if (!intra)
					;
				else
					ProcessIntraMbImplStd(&mb, 1);
				di = mb._distortion[i];
			}//end while i...

			/// Compensate only if a solution was found otherwise QP = H264V2_MAX_QP is the
			/// best that can be done. It is expected to step back only once but if the step
			/// up process above was not implemented because di > Dmax then iterating down
			/// several times may be required.
			if (di > Dmax)
			{
				/// Iterate down from i. If i drops to lowestQ without di <= Dmax
				/// then leave as it is the best that can be done.
				while ((i > lowestQP) && (di > Dmax))
				{
					i -= MbStepSize[i];
					if (i < lowestQP)
						i = lowestQP;

					mb._mbQP = i;
					if (!intra)
						;
					else
						ProcessIntraMbImplStd(&mb, 1);
					di = mb._distortion[i];
				}//end while i...

			}//end if di...

		}//end if i...

	}//end else...

	/// Re-sync the mb if it was not already processed.
	if ((mbIndex >= lclChangeMb) && (!atLeastOnce))
	{
		mb._mbQP = i;
		if (!intra)
			;
		else
			ProcessIntraMbImplStd(&mb, 1);
	}//end if mbIndex...

	/// Return QP value where dist just less than Dmax for this macroblock.
	*changeMb = lclChangeMb;
	return(i);
}//end GetMbQPBelowDmaxApprox.

/** The forward and inverse loop of a Std Intra macroblock.
Code factoring usage. Includes reading the input image, IT, quantisation,
vlc encoding and determining coding patterns, type, etc. Return the
bit cost.
@param pMb		: Macroblock to operate on.
@param withDR	: With distortion-rate calculations. If = 2 then distortion only (no rate calc.)
@return				: Bit cost.
*/
int H264v2Codec::ProcessIntraMbImplStd(MacroBlockH264* pMb, int withDR)
{
	/// NB: _mbQP must be correctly defined before this method is called.
	pMb->_mbEncQP = pMb->_mbQP; ///< This method may alter the mb QP therefore keep an orignal copy.

	if (pMb->_mbQP > H264V2_MAX_QP) ///< Special conditions apply for out of range QP values.
		pMb->_mbQP = H264V2_MAX_QP;

	int lOffX = pMb->_offLumX;
	int lOffY = pMb->_offLumY;
	int cOffX = pMb->_offChrX;
	int cOffY = pMb->_offChrY;

	/// Base settings for all intra macroblocks.
	pMb->_skip = 0;
	pMb->_intraFlag = 1;

	///------------------- Image prediction and loading ----------------------------------------
	/// Currently only Intra 16x16 mode implemented. 
	pMb->_mbPartPredMode = MacroBlockH264::Intra_16x16;

	/// Predict the input from the previously decoded neighbour ref macroblocks then subtract the
	/// prediction from the input.

	/// Lum...
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY); ///< Align the Ref Lum img block with this macroblock.
	_Lum->SetOverlayDim(16, 16);
	_Lum->SetOrigin(lOffX, lOffY);		///< Align the Lum img block with this macroblock.
	/// Select the best mode and get the prediction. Pred stored in _16x16.
	pMb->_intra16x16PredMode = GetIntra16x16LumPredAndMode(pMb, _Lum, _RefLum, _16x16);

	_Lum->Read(*(_RefLum));						///< Read from input Lum into ref Lum.
	_RefLum->Sub16x16(*(_16x16));			///< Subtract pred from ref Lum and leave result in ref img.

	  /// ... and Chr components.
	_RefCb->SetOverlayDim(8, 8);
	_RefCr->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCr->SetOrigin(cOffX, cOffY);
	_Cb->SetOverlayDim(8, 8);
	_Cb->SetOrigin(cOffX, cOffY);
	_Cr->SetOverlayDim(8, 8);
	_Cr->SetOrigin(cOffX, cOffY);
	pMb->_intraChrPredMode = GetIntra8x8ChrPredAndMode(pMb, _Cb, _Cr, _RefCb, _RefCr, _8x8_0, _8x8_1);

	_Cb->Read(*(_RefCb));
	_RefCb->Sub8x8(*(_8x8_0));
	_Cr->Read(*(_RefCr));
	_RefCr->Sub8x8(*(_8x8_1));

	/// Fill all the non-DC 4x4 blks (Not blks = -1, 17, 18) of the macroblock blocks with 
	/// the differnce Lum and Chr after prediction.
	MacroBlockH264::LoadBlks(pMb, _RefLum, lOffX, lOffY, _RefCb, _RefCr, cOffX, cOffY);

	/// ------------------ Transform & Quantisation with Inverse -----------------------------------
	int mbDistortion = 0;
	if (pMb->_mbEncQP <= H264V2_MAX_QP)
	{
		/// ------------------ Calc distortion from the feedback loop --------------------------------
		/// Implement the forward and feedback loop into the ref image. The forward block coeffs are 
	  /// still required for the context-aware vlc coding and therefore the temporary working blocks 
	  /// are used for this feedback loop.
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			mbDistortion = TransAndQuantIntra16x16MBlkWithInv(pMb, withDR);
	}//end if_mbEncQP...
	else ///< if( pMb->_mbEncQP > H264V2_MAX_QP)
	{
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			TransAndQuantIntra16x16MBlk(pMb);

		/// ------------------- Zero Coeffs for QP > H264V2_MAX_QP -------------------------------
		CoeffZeroingQuantisation(pMb);

		///---------------------- Inverse Transform & Quantisation -------------------------------
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			InverseTransAndQuantIntra16x16MBlk(pMb, 1);

		///---------------------- Lum Distortion -------------------------------------------------
		if (withDR)
		{
			/// Ref holds the pre-quant diff with the pred and the temp blks hold the post-quant diff
			/// to compare with.
			_RefLum->SetOverlayDim(4, 4);
			int i, j;
			for (i = 0; i < 4; i++)
				for (j = 0; j < 4; j++)
				{
					_RefLum->SetOrigin(lOffX + 4 * j, lOffY + 4 * i); ///< Align with blk.
#ifdef USE_ABSOLUTE_DIFFERENCE
					mbDistortion += _RefLum->Tad4x4(*(pMb->_lumBlkTmp[i][j].GetBlkOverlay()));
#else
					mbDistortion += _RefLum->Tsd4x4(*(pMb->_lumBlkTmp[i][j].GetBlkOverlay()));
#endif
				}//end for i & j...
		}//end if withDR...

	}//end else...

	/// --------------------- Set distortion -------------------------------------------------
	if (withDR)
		pMb->_distortion[pMb->_mbEncQP] = mbDistortion;

	/// --------------------- Image Storing into Ref -----------------------------------------
	/// Fill the ref (difference) lum and chr from all the non-DC 4x4 
	/// blks (i.e. Not blks = -1, 17, 18) of the macroblock temp blocks. 
	MacroBlockH264::StoreBlks(pMb, _RefLum, lOffX, lOffY, _RefCb, _RefCr, cOffX, cOffY, 1);

	/// --------------------- Add the prediction ---------------------------------------------
	/// Lum.
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY);			    ///< Align the Ref Lum img block with this macroblock.
	_RefLum->Add16x16WithClip255(*(_16x16));	///< Add pred to ref Lum and leave result in ref img.
	/// Cb.
	_RefCb->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCb->Add8x8WithClip255(*(_8x8_0));
	/// Cr.
	_RefCr->SetOverlayDim(8, 8);
	_RefCr->SetOrigin(cOffX, cOffY);
	_RefCr->Add8x8WithClip255(*(_8x8_1));

	/// ------------------ Set patterns and type -----------------------------------------------
	/// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	/// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the delta quantisation parameter
	pMb->_mb_qp_delta = GetDeltaQP(pMb);

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// ------------------ Calc the Rate and store results -----------------------------------------
	/// Store rate for this quant value.
	if (withDR == 1) ///< For withDR = 2, no rate calculation is done.
		pMb->_rate[pMb->_mbEncQP] = MacroBlockLayerBitCounter(pMb);

	return(pMb->_rate[pMb->_mbEncQP]);
}//end ProcessIntraMbImplStd.

/** The forward and inverse loop of a Std Intra macroblock.
Code factoring usage. Includes reading the input image, IT, quantisation,
vlc encoding and determining coding patterns, type, etc. Return the
bit cost.
@param pMb		      : Macroblock to operate on.
@param withDR	      : With distortion-rate calculations. If = 2 then distortion only (no rate calc.)
@param usePrevPred  : Use previous prediction mode.
@return				      : Bit cost.
*/
int H264v2Codec::ProcessIntraMbImplStd(MacroBlockH264* pMb, int withDR, int usePrevPred)
{
	/// NB: _mbQP must be correctly defined before this method is called.
	pMb->_mbEncQP = pMb->_mbQP; ///< This method may alter the mb QP therefore keep an orignal copy.

	if (pMb->_mbQP > H264V2_MAX_QP) ///< Special conditions apply for out of range QP values.
		pMb->_mbQP = H264V2_MAX_QP;

	int lOffX = pMb->_offLumX;
	int lOffY = pMb->_offLumY;
	int cOffX = pMb->_offChrX;
	int cOffY = pMb->_offChrY;

	/// Base settings for all intra macroblocks.
	pMb->_skip = 0;
	pMb->_intraFlag = 1;

	///------------------- Image prediction and loading ----------------------------------------
	/// Currently only Intra 16x16 mode implemented. 
	pMb->_mbPartPredMode = MacroBlockH264::Intra_16x16;

	/// Predict the input from the previously decoded neighbour ref macroblocks then subtract the
	/// prediction from the input.

	/// Lum...
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY); ///< Align the Ref Lum img block with this macroblock.
	_Lum->SetOverlayDim(16, 16);
	_Lum->SetOrigin(lOffX, lOffY);		///< Align the Lum img block with this macroblock.
	/// Select the best mode or use the previous mode and get the prediction. Pred stored in _16x16.
	if (usePrevPred)
	{
		switch (pMb->_intra16x16PredMode)
		{
		case MacroBlockH264::Intra_16x16_Vert:
			GetIntraVertPred(pMb, _RefLum, _16x16, 1);
			break;
		case MacroBlockH264::Intra_16x16_Horiz:
			GetIntraHorizPred(pMb, _RefLum, _16x16, 1);
			break;
		case MacroBlockH264::Intra_16x16_DC:
			GetIntra16x16LumDCPred(pMb, _RefLum, _16x16);
			break;
		case MacroBlockH264::Intra_16x16_Plane:
			GetIntra16x16LumPlanePred(pMb, _RefLum, _16x16);
			break;
		}//end switch _intra16x16PredMode...
	}//end if usePrevPred...
	else
		pMb->_intra16x16PredMode = GetIntra16x16LumPredAndMode(pMb, _Lum, _RefLum, _16x16);

	_Lum->Read(*(_RefLum));						///< Read from input Lum into ref Lum.
	_RefLum->Sub16x16(*(_16x16));			///< Subtract pred from ref Lum and leave result in ref img.

	  /// ... and Chr components.
	_RefCb->SetOverlayDim(8, 8);
	_RefCr->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCr->SetOrigin(cOffX, cOffY);
	_Cb->SetOverlayDim(8, 8);
	_Cb->SetOrigin(cOffX, cOffY);
	_Cr->SetOverlayDim(8, 8);
	_Cr->SetOrigin(cOffX, cOffY);
	if (usePrevPred)
	{
		switch (pMb->_intraChrPredMode)
		{
		case MacroBlockH264::Intra_Chr_DC:
			GetIntra8x8ChrDCPred(pMb, _RefCb, _8x8_0);
			GetIntra8x8ChrDCPred(pMb, _RefCr, _8x8_1);
			break;
		case MacroBlockH264::Intra_Chr_Horiz:
			GetIntraHorizPred(pMb, _RefCb, _8x8_0, 0);
			GetIntraHorizPred(pMb, _RefCr, _8x8_1, 0);
			break;
		case MacroBlockH264::Intra_Chr_Vert:
			GetIntraVertPred(pMb, _RefCb, _8x8_0, 0);
			GetIntraVertPred(pMb, _RefCr, _8x8_1, 0);
			break;
		case MacroBlockH264::Intra_Chr_Plane:
			GetIntra8x8ChrPlanePred(pMb, _RefCb, _8x8_0);
			GetIntra8x8ChrPlanePred(pMb, _RefCr, _8x8_1);
			break;
		}//end switch _intraChrPredMode...
	}//end if usePrevPred...
	else
		pMb->_intraChrPredMode = GetIntra8x8ChrPredAndMode(pMb, _Cb, _Cr, _RefCb, _RefCr, _8x8_0, _8x8_1);

	_Cb->Read(*(_RefCb));
	_RefCb->Sub8x8(*(_8x8_0));
	_Cr->Read(*(_RefCr));
	_RefCr->Sub8x8(*(_8x8_1));

	/// Fill all the non-DC 4x4 blks (Not blks = -1, 17, 18) of the macroblock blocks with 
	/// the differnce Lum and Chr after prediction.
	MacroBlockH264::LoadBlks(pMb, _RefLum, lOffX, lOffY, _RefCb, _RefCr, cOffX, cOffY);

	/// ------------------ Transform & Quantisation with Inverse -----------------------------------
	int mbDistortion = 0;
	if (pMb->_mbEncQP <= H264V2_MAX_QP)
	{
		/// ------------------ Calc distortion from the feedback loop --------------------------------
		/// Implement the forward and feedback loop into the ref image. The forward block coeffs are 
	  /// still required for the context-aware vlc coding and therefore the temporary working blocks 
	  /// are used for this feedback loop. The distortion returned includes ROI modification.
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			mbDistortion = TransAndQuantIntra16x16MBlkWithInv(pMb, withDR);
	}//end if_mbEncQP...
	else ///< if( pMb->_mbEncQP > H264V2_MAX_QP)
	{
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			TransAndQuantIntra16x16MBlk(pMb);

		/// ------------------- Zero Coeffs for QP > H264V2_MAX_QP -------------------------------
		CoeffZeroingQuantisation(pMb);

		///---------------------- Inverse Transform & Quantisation -------------------------------
		if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
			InverseTransAndQuantIntra16x16MBlk(pMb, 1);

		///---------------------- Lum Distortion -------------------------------------------------
		if (withDR)
		{
			/// Ref holds the pre-quant diff with the pred and the temp blks hold the post-quant diff
			/// to compare with.
			_RefLum->SetOverlayDim(4, 4);
			int i, j;
			for (i = 0; i < 4; i++)
				for (j = 0; j < 4; j++)
				{
					_RefLum->SetOrigin(lOffX + 4 * j, lOffY + 4 * i); ///< Align with blk.
#ifdef USE_ABSOLUTE_DIFFERENCE
					mbDistortion += _RefLum->Tad4x4(*(pMb->_lumBlkTmp[i][j].GetBlkOverlay()));
#else
					mbDistortion += _RefLum->Tsd4x4(*(pMb->_lumBlkTmp[i][j].GetBlkOverlay()));
#endif
				}//end for i & j...

      /// Region of interest modification.
      mbDistortion = ROIDistortion(pMb->_mbIndex, mbDistortion);
		}//end if withDR...

	}//end else...

	/// --------------------- Set distortion -------------------------------------------------
  if (withDR)
    pMb->_distortion[pMb->_mbEncQP] = mbDistortion;

	/// --------------------- Image Storing into Ref -----------------------------------------
	/// Fill the ref (difference) lum and chr from all the non-DC 4x4 
	/// blks (i.e. Not blks = -1, 17, 18) of the macroblock temp blocks. 
	MacroBlockH264::StoreBlks(pMb, _RefLum, lOffX, lOffY, _RefCb, _RefCr, cOffX, cOffY, 1);

	/// --------------------- Add the prediction ---------------------------------------------
	/// Lum.
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY);			    ///< Align the Ref Lum img block with this macroblock.
	_RefLum->Add16x16WithClip255(*(_16x16));	///< Add pred to ref Lum and leave result in ref img.
	/// Cb.
	_RefCb->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCb->Add8x8WithClip255(*(_8x8_0));
	/// Cr.
	_RefCr->SetOverlayDim(8, 8);
	_RefCr->SetOrigin(cOffX, cOffY);
	_RefCr->Add8x8WithClip255(*(_8x8_1));

	/// ------------------ Set patterns and type -----------------------------------------------
	/// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	/// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the delta quantisation parameter
	pMb->_mb_qp_delta = GetDeltaQP(pMb);

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// ------------------ Calc the Rate and store results -----------------------------------------
	/// Store rate for this quant value.
	if (withDR == 1) ///< For withDR = 2, no rate calculation is done.
		pMb->_rate[pMb->_mbEncQP] = MacroBlockLayerBitCounter(pMb);

	return(pMb->_rate[pMb->_mbEncQP]);
}//end ProcessIntraMbImplStd.

/** The forward and inverse loop of a Std Intra macroblock with a max distortion criterion.
Code factoring usage. Includes reading the input image, IT, quantisation, vlc encoding and
determining coding patterns, type, etc. _mbQP is decremented until the lum distortion is
below Dmax. If distortion for the given _mbQP is below Dmax then the mb is processed only
once, there is no ascent search. _mbQP must be less than or equal to H264V2_MAX_QP, coeff
zeroing is not implemented in this overload. Return the bit cost.
@param pMb		      : Macroblock to operate on.
@param withDR	      : With distortion-rate calculations. If = 2 then distortion only (no rate calc.)
@param usePrevPred  : Use previous prediction mode.
@param Dmax         : Max distortion for this mb.
@param minQP        : Lower limit for the QP descent.
@return				      : Bit cost.
*/
int H264v2Codec::ProcessIntraMbImplStd(MacroBlockH264* pMb, int withDR, int usePrevPred, int Dmax, int minQP)
{
	/// NB: _mbQP must be correctly defined as the starting QP value before this method is called.

  /// The delta QP for a mb is constrained to {-26...25}. 
	int q = pMb->_mbQP;

	int prevQP = GetPrevMbQP(pMb);
	int lowQP = prevQP - 26;
	if (lowQP < minQP) lowQP = minQP;
	int highQP = prevQP + 25;
	if (highQP > H264V2_MAX_QP) highQP = H264V2_MAX_QP;
	if (q < lowQP) q = lowQP;
	else if (q > highQP) q = highQP;
	pMb->_mbQP = q;

	int lOffX = pMb->_offLumX;
	int lOffY = pMb->_offLumY;
	int cOffX = pMb->_offChrX;
	int cOffY = pMb->_offChrY;

	/// Base settings for all intra macroblocks.
	pMb->_skip = 0;
	pMb->_intraFlag = 1;

	///------------------- Image prediction and loading ----------------------------------------
	/// Currently only Intra 16x16 mode implemented. 
	pMb->_mbPartPredMode = MacroBlockH264::Intra_16x16;

	/// Predict the input from the previously decoded neighbour ref macroblocks then subtract the
	/// prediction from the input.

	/// Lum...
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY); ///< Align the Ref Lum img block with this macroblock.
	_Lum->SetOverlayDim(16, 16);
	_Lum->SetOrigin(lOffX, lOffY);		///< Align the Lum img block with this macroblock.

  /// Frame luma signal MAD and MSD accumulation. These variables were cleared before processing the frame.
  /// The calculation is only defined within the mb and from col 1 row 0 onwards, the first pel in each row
  /// is then compared with the one above.
	if (withDR)
	{
		for (int r = 0; r < 16; r++)
			for (int c = 0; c < 16; c++)
			{
				if (r || c)  ///< Not (0,0)
				{
					int curr, prev;
					curr = _Lum->Read(c, r);
					if (c)
						prev = _Lum->Read(c - 1, r); ///< Left.
					else
						prev = _Lum->Read(c, r - 1); ///< Above.

					_frameMAD += DISTORTIONABSDIFF(curr, prev);
					_frameMSD += DISTORTIONSQRDIFF(curr, prev);
				}//end if r...
			}//end for r & c...

		_frameMAD_N += 255;
	}//end if withDR...

	  /// Select the best mode or use the previous mode and get the prediction. Pred stored in _16x16.
	if (usePrevPred)
	{
		switch (pMb->_intra16x16PredMode)
		{
		case MacroBlockH264::Intra_16x16_Vert:
			GetIntraVertPred(pMb, _RefLum, _16x16, 1);
			break;
		case MacroBlockH264::Intra_16x16_Horiz:
			GetIntraHorizPred(pMb, _RefLum, _16x16, 1);
			break;
		case MacroBlockH264::Intra_16x16_DC:
			GetIntra16x16LumDCPred(pMb, _RefLum, _16x16);
			break;
		case MacroBlockH264::Intra_16x16_Plane:
			GetIntra16x16LumPlanePred(pMb, _RefLum, _16x16);
			break;
		}//end switch _intra16x16PredMode...
	}//end if usePrevPred...
	else
		pMb->_intra16x16PredMode = GetIntra16x16LumPredAndMode(pMb, _Lum, _RefLum, _16x16);

	_Lum->Read(*(_RefLum));						///< Read from input Lum into ref Lum.
	_RefLum->Sub16x16(*(_16x16));			///< Subtract pred from ref Lum and leave result in ref img.

	  /// ... and Chr components.
	_RefCb->SetOverlayDim(8, 8);
	_RefCr->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCr->SetOrigin(cOffX, cOffY);
	_Cb->SetOverlayDim(8, 8);
	_Cb->SetOrigin(cOffX, cOffY);
	_Cr->SetOverlayDim(8, 8);
	_Cr->SetOrigin(cOffX, cOffY);
	if (usePrevPred)
	{
		switch (pMb->_intraChrPredMode)
		{
		case MacroBlockH264::Intra_Chr_DC:
			GetIntra8x8ChrDCPred(pMb, _RefCb, _8x8_0);
			GetIntra8x8ChrDCPred(pMb, _RefCr, _8x8_1);
			break;
		case MacroBlockH264::Intra_Chr_Horiz:
			GetIntraHorizPred(pMb, _RefCb, _8x8_0, 0);
			GetIntraHorizPred(pMb, _RefCr, _8x8_1, 0);
			break;
		case MacroBlockH264::Intra_Chr_Vert:
			GetIntraVertPred(pMb, _RefCb, _8x8_0, 0);
			GetIntraVertPred(pMb, _RefCr, _8x8_1, 0);
			break;
		case MacroBlockH264::Intra_Chr_Plane:
			GetIntra8x8ChrPlanePred(pMb, _RefCb, _8x8_0);
			GetIntra8x8ChrPlanePred(pMb, _RefCr, _8x8_1);
			break;
		}//end switch _intraChrPredMode...
	}//end if usePrevPred...
	else
		pMb->_intraChrPredMode = GetIntra8x8ChrPredAndMode(pMb, _Cb, _Cr, _RefCb, _RefCr, _8x8_0, _8x8_1);

	_Cb->Read(*(_RefCb));
	_RefCb->Sub8x8(*(_8x8_0));
	_Cr->Read(*(_RefCr));
	_RefCr->Sub8x8(*(_8x8_1));

	/// Fill all the non-DC 4x4 blks (Not blks = -1, 17, 18) of the macroblock blocks with 
	/// the differnce Lum and Chr after prediction.
	MacroBlockH264::LoadBlks(pMb, _RefLum, lOffX, lOffY, _RefCb, _RefCr, cOffX, cOffY);

	/// ------------------ Transform & Quantisation --------------------------------------------
	/// Implement the forward and feedback loop into the temp blks. The block coeffs are still 
  /// required for the context-aware vlc coding and therefore the temporary working blocks are
  /// used in the feedback loop. The mb QP will change during the TransAndQuantIntra16x16MBlk() 
  /// method call.
	if (pMb->_mbPartPredMode == MacroBlockH264::Intra_16x16)
		pMb->_distortion[pMb->_mbEncQP] = TransAndQuantIntra16x16MBlk(pMb, Dmax, lowQP);

	/// ------------------ Feedback loop -------------------------------------------------------

	/// --------------------- Image Storing into Ref -----------------------------------------
	/// Fill the ref (difference) lum and chr from all the non-DC 4x4 
	/// blks (i.e. Not blks = -1, 17, 18) of the macroblock temp blocks. 
	MacroBlockH264::StoreBlks(pMb, _RefLum, lOffX, lOffY, _RefCb, _RefCr, cOffX, cOffY, 1);

	/// --------------------- Add the prediction ---------------------------------------------
	/// Lum.
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY);			    ///< Align the Ref Lum img block with this macroblock.
	_RefLum->Add16x16WithClip255(*(_16x16));	///< Add pred to ref Lum and leave result in ref img.
	/// Cb.
	_RefCb->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCb->Add8x8WithClip255(*(_8x8_0));
	/// Cr.
	_RefCr->SetOverlayDim(8, 8);
	_RefCr->SetOrigin(cOffX, cOffY);
	_RefCr->Add8x8WithClip255(*(_8x8_1));

	/// Distortion with Lum input was calculated in the TransAndQuantIntra16x16MBlk() method. 

	  /// ------------------ Set patterns and type -----------------------------------------------
	  /// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	  /// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the delta quantisation parameter
	pMb->_mb_qp_delta = GetDeltaQP(pMb);

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// ------------------ Calc the Rate and store results -----------------------------------------
	/// Store rate for this quant value.
	if (withDR == 1) ///< For withDR = 2, no rate calculation is done.
	{
		if ((_modeOfOperation == H264V2_MINMAX_RATECNT)|| (_modeOfOperation == H264V2_MINAVG_RATECNT)) ///< Requires coeff bits without the mb header bits.
			pMb->_rate[pMb->_mbEncQP] = MacroBlockLayerCoeffBitCounter(pMb);
		else
			pMb->_rate[pMb->_mbEncQP] = MacroBlockLayerBitCounter(pMb);
	}//end if withDR...

	return(pMb->_rate[pMb->_mbEncQP]);
}//end ProcessIntraMbImplStd.

/** Extreme quantisation for large QP values by zeroing coeffs.
For mb QP values set to greater than the max for the H.264 standard, zero the coeffs in
and ordered manner. This method must be called after the forward transform and quantisation
but before the inverse process. Used for code factoring.
@param pMb	: Macroblock to operate on.
@return		  : none.
*/
void H264v2Codec::CoeffZeroingQuantisation(MacroBlockH264* pMb)
{
	int blk, i;
	const int *pZZ4x4 = CAVLCH264Impl::zigZag4x4Pos;
	const int *pZZ2x2 = CAVLCH264Impl::zigZag2x2Pos;

	/// There are 16 (4x4) AC coeffs per blk and the _mbEncQP value indicates how many of
	/// these are to be zeroed in reverse zig-zag order. _mbEncQP = {52..66} implies that 
	/// {1..15} AC coeffs of lum and chr are zeroed. {67..69} The AC coeffs of the Chr DC 
	/// 2x2 coeffs additionally zeroed. {70..84} AC coeffs of the Lum DC coeffs additionally 
	/// zeroed. {85} DC coeff of the Chr DC 2x2 coeffs zeroed. {86} DC coeff of the Lum DC 
	/// coeffs zeroed.
	int acCoeffZeroed = 15; ///< By default.
	int chrDcCoeffZeroed = 0;
	int lumDcCoeffZeroed = 0;
	if (pMb->_mbEncQP <= (H264V2_MAX_QP + 15))
		acCoeffZeroed = pMb->_mbEncQP - H264V2_MAX_QP;
	else if (pMb->_mbEncQP <= 69)  ///< Include AC coeff of Chr DC for QP = {67..69}.
	{
		chrDcCoeffZeroed = pMb->_mbEncQP - 66;
	}//end else if _mbEncQP...
	else if (pMb->_mbEncQP <= 84)  ///< Include AC coeffs of Lum DC and all AC coeffs of Chr DC for QP = {70..84}.
	{
		acCoeffZeroed = 16;
		chrDcCoeffZeroed = 3; ///< Not the Chr DC term.
		lumDcCoeffZeroed = pMb->_mbEncQP - 69;
	}//end else if _mbEncQP...
	else if (pMb->_mbEncQP <= 85)  ///< Include all Chr DC coeffs but only AC coeffs of Lum DC for QP = {85}.
	{
		acCoeffZeroed = 16;
		chrDcCoeffZeroed = 4; ///< ...and Chr DC term.
		lumDcCoeffZeroed = 15;
	}//end else if _mbEncQP...
	else  ///< All zeroed for QP = {86} and above.
	{
		acCoeffZeroed = 16;
		chrDcCoeffZeroed = 4;
		lumDcCoeffZeroed = 16;
	}//end else...

	int startPos4x4 = 16 - acCoeffZeroed;
	int startChrDcPos2x2 = 4 - chrDcCoeffZeroed;
	int startLumDcPos4x4 = 16 - lumDcCoeffZeroed;

	/// Zero coeff in the mb according to the QP rules above.
	for (blk = MBH264_LUM_DC; blk < MBH264_NUM_BLKS; blk++)
	{
		BlockH264* pBlk = pMb->_blkParam[blk].pBlk;
		short* pB = pBlk->GetBlk();

		if ((blk != MBH264_LUM_DC) && (blk != MBH264_CB_DC) && (blk != MBH264_CR_DC))  ///< Exclude DC blks.
		{
			for (i = startPos4x4; i < 16; i++)
			{
				if (pB[pZZ4x4[i]])  ///< Only need to change coeffs that are not already zero.
					pB[pZZ4x4[i]] = 0;
			}//end for i...
		}//end if blk...
		else if ((blk == MBH264_CB_DC) || (blk == MBH264_CR_DC)) ///< Chr DC blks AND zero'ing required.
		{
			if (chrDcCoeffZeroed)
			{
				for (i = startChrDcPos2x2; i < 4; i++)
				{
					if (pB[pZZ2x2[i]])  ///< Only need to change coeffs that are not already zero.
						pB[pZZ2x2[i]] = 0;
				}//end for i...
			}//end if chrDcCoeffZeroed...
		}//end else if blk...
		else ///< if(blk == MBH264_LUM_DC) Lum DC AND zero'ing required.
		{
			if (lumDcCoeffZeroed)
			{
				for (i = startLumDcPos4x4; i < 16; i++)
				{
					if (pB[pZZ4x4[i]])  ///< Only need to change coeffs that are not already zero.
						pB[pZZ4x4[i]] = 0;
				}//end for i...
			}//end if lumDcCoeffZeroed...
		}//end else...

	}//end for blk...

}//end CoeffZeroingQuantisation.

/** Extreme quantisation for large QP values by zeroing coeffs.
For mb QP values set to greater than the max for the H.264 standard, zero the coeffs in
an ordered manner. This method must be called after the forward transform and quantisation
but before the inverse process. Used for code factoring. Ver 2 optimises the process used
in the original version.
@param pMb	: Macroblock to operate on.
@return		  : none.
*/
void H264v2Codec::CoeffZeroingQuantisationVer2(MacroBlockH264* pMb)
{
	//////////////////////////////////////////////////////////////////////////
	  // if(pMb->_mbEncQP <= H264V2_MAX_QP + 15) return; ///< Why are we here?
	//////////////////////////////////////////////////////////////////////////

	int blk, i;
	const int *pZZ4x4 = CAVLCH264Impl::zigZag4x4Pos;
	const int *pZZ2x2 = CAVLCH264Impl::zigZag2x2Pos;

	/// There are 16 (4x4) AC coeffs per blk and the _mbEncQP value indicates how many of
	/// these are to be zeroed in reverse zig-zag order. _mbEncQP = {52..66} implies that 
	/// {1..15} AC coeffs are zeroed. {67..70} Chr DC 2x2 coeffs additionally zeroed. {71} 
	/// Lum DC coeffs additionally zeroed.
	int acCoeffZeroed = 15; ///< By default.
	int chrDcCoeffZeroed = 0;
	if (pMb->_mbEncQP <= (H264V2_MAX_QP + 15))
		acCoeffZeroed = pMb->_mbEncQP - H264V2_MAX_QP;
	else if (pMb->_mbEncQP <= 70)  ///< Include Chr DC coeffs for QP = {67..70}.
		chrDcCoeffZeroed = pMb->_mbEncQP - 66;
	else ///< if( pMb->_mbEncQP == 71 )  Include Lum and Chr DC coeffs.
	{
		acCoeffZeroed = 16;
		chrDcCoeffZeroed = 4;
	}//end else...
	int startPos4x4 = 16 - acCoeffZeroed;
	int startPos2x2 = 4 - chrDcCoeffZeroed;

	/// Zero coeff in the mb according to the QP rules above.
	for (blk = MBH264_LUM_0_0; blk < MBH264_NUM_BLKS; blk++)
	{
		BlockH264* pBlk = pMb->_blkParam[blk].pBlk;
		short* pB = pBlk->GetBlk();

		if ((blk != MBH264_CB_DC) && (blk != MBH264_CR_DC))  ///< Exclude Chr DC blks.
		{
			for (i = startPos4x4; i < 16; i++)
			{
				if (pB[pZZ4x4[i]])  ///< Only need to change coeffs that are not already zero.
					pB[pZZ4x4[i]] = 0;
			}//end for i...
		}//end if blk...
		else  ///< Chr DC blks AND zero'ing required.
		{
			if (chrDcCoeffZeroed)
			{
				for (i = startPos2x2; i < 4; i++)
				{
					if (pB[pZZ2x2[i]])  ///< Only need to change coeffs that are not already zero.
						pB[pZZ2x2[i]] = 0;
				}//end for i...
			}//end if chrDcCoeffZeroed...
		}//end else...

	}//end for blk...

}//end CoeffZeroingQuantisationVer2.

/** The absolute min bit encoding of a Std Intra macroblock.
Set and measure the encoding for a min bit implementation of the std Intra
macroblock. This only includes DC prediction and all residual DC and AC
coeffs are zero'ed. QP is set to max. Return the bit cost.
@param pMb	: Macroblock to operate on.
@return		  : Bit cost.
*/
int H264v2Codec::ProcessIntraMbImplStdMin(MacroBlockH264* pMb)
{
	int rate = 0;
	int lOffX = pMb->_offLumX;
	int lOffY = pMb->_offLumY;
	int cOffX = pMb->_offChrX;
	int cOffY = pMb->_offChrY;

	/// Base settings for all intra macroblocks.
	pMb->_mbQP = H264V2_MAX_QP;
	pMb->_skip = 0;
	pMb->_intraFlag = 1;

	///------------------- Image prediction and loading ----------------------------------------
	/// Currently only Intra 16x16 mode implemented. 
	pMb->_mbPartPredMode = MacroBlockH264::Intra_16x16;

	/// Predict the input from the previously decoded neighbour ref macroblocks then subtract the
	/// prediction from the input.

	/// Lum...
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY); ///< Align the Ref Lum img block with this macroblock.
	/// Force the selection to DC Lum prediction for all macroblocks.
	pMb->_intra16x16PredMode = MacroBlockH264::Intra_16x16_DC;
	GetIntra16x16LumDCPred(pMb, _RefLum, _16x16);

	_RefLum->Write16x16(*(_16x16)); ///< Write the prediction into the ref.

	  /// ... and Chr components.
	_RefCb->SetOverlayDim(8, 8);
	_RefCr->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCr->SetOrigin(cOffX, cOffY);

	pMb->_intraChrPredMode = MacroBlockH264::Intra_Chr_DC;
	GetIntra8x8ChrDCPred(pMb, _RefCb, _8x8_0);
	GetIntra8x8ChrDCPred(pMb, _RefCr, _8x8_1);

	_RefCb->Write8x8(*(_8x8_0));
	_RefCr->Write8x8(*(_8x8_1));

	/// Clear all mb coeffs for encoding. Implies all difference DC and AC coeffs are zero.
	for (int blk = 0; blk < MBH264_NUM_BLKS; blk++)
	{
		BlockH264* pBlk = pMb->_blkParam[blk].pBlk;
		(pBlk->GetBlkOverlay())->Clear();
		pBlk->SetNumCoeffs(0);
	}//end for blk...

	/// ------------------ Set patterns and type -----------------------------------------------
	/// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	/// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the delta quantisation parameter. For Intra slices
	/// no previous macroblocks are skipped.
	int prevMbIdx = pMb->_mbIndex - 1;
	if (prevMbIdx >= 0)	///< Previous macroblock is within the image boundaries.
	{
		if (pMb->_slice == _pMb[prevMbIdx]._slice)	///< Previous macroblock within same slice.
			pMb->_mb_qp_delta = pMb->_mbQP - _pMb[prevMbIdx]._mbQP;
		else
			pMb->_mb_qp_delta = pMb->_mbQP - _slice._qp;
	}//end if prevMbIdx...
	else
		pMb->_mb_qp_delta = pMb->_mbQP - _slice._qp;

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// ------------------ Calc the Rate --------------------------------------------------------
	rate = MacroBlockLayerBitCounter(pMb);

	return(rate);
}//end ProcessIntraMbImplStdMin.

/** Encode the image data for Inter pictures.
Process all macroblocks in order and only write the result to the ref image
space if the writeRef code is set. Bit 1 of writeRef refers to motion compensation
and bit 0 to adding the difference to the ref. The motion estimation process with
its associated data structures is assumed to have been completed before this
method is called. The macroblock obj is prepared for coding onto the bit stream.
Note: For iteratively calling this method;
	Call 1:			writeRef = 2 (compensation to ref but no adding).
	Call 2 - X: writeRef = 0 (ref is motion compensated already from call 1).
	Call final:	writeRef = 1 (allow final add to ref).
@param allowedBits	: Total remaining bits to target.
@param bitsUsed			: Num of bits used for this encoding.
@param writeRef			: Bit code 1x/0x = do/do not comp. x1/x0 do/do not add ref.
@return							: 1 = success, 0 = error.
*/
int H264v2Codec::InterImgPlaneEncoderImplStdVer1::Encode(int allowedBits, int* bitsUsed, int writeRef)
{
	/// Whip through each macroblock and encode. The stream writing of
	/// the macroblock is seperate to allow further rate-distortion decision 
	/// making later on in the encoding process.
	int compRef = writeRef & 2;
	int addRef = writeRef & 1;
	int len = _codec->_mbLength;

	/// Slice without partitioning and therefore only one set of slice parameters.

	/// Set up the input and ref image mem overlays.
	_codec->_Lum->SetOverlayDim(4, 4);
	_codec->_Cb->SetOverlayDim(4, 4);
	_codec->_Cr->SetOverlayDim(4, 4);
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOverlayDim(8, 8);

	/// All integer transforms are Inter in this method.
	_codec->_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 0);
	_codec->_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TChr->SetParameter(IForwardTransform::INTRA_FLAG_ID, 0);
	/// By default the DC transforms were set in the TransformOnly mode in the Open() method.

	/// Motion estimation has been previously performed outside of this method and therefore
	/// only motion compensation is required here. Prepare for motion compensation on a per 
	/// macoblock basis. The vectors themselves are held in _pMotionEstimationResult.
	if (compRef)
		_codec->_pMotionCompensator->PrepareForSingleVectorMode();

	/// Get the motion vector list to work with. Assume SIMPLE2D type list as only a
	/// single 16x16 motion vector is considered per macroblock in this implementation
	/// i.e. Inter_16x16 mode only
	int	listLen = _codec->_pMotionEstimationResult->GetLength();
	if (listLen != len)
	{
		*bitsUsed = 0;
		return(0);	///< Error: Motion vector list must match.
	}//end if listLen...

  /////////////////////////////////////////////////////////////////////////////////////////////
  /// Research Data Collection: Create an img to capture the difference between the input img 
  /// and the motion comp pred img. The collection is done per mb and dumped to file after.
  //int selectedFrameNum = 0;
  ////int selectedFrameList[15] = {19, 78, 93, 148, 152, 154, 156, 173, 182, 194, 204, 207, 213, 216, 221}; ///< Foreman
  ////int selectedFrameList[7] = {24, 62, 126, 184, 210, 258, 278}; ///< Mobile
  //int selectedFrameList[9] = {15, 24, 52, 114, 124, 152, 226, 256, 288}; ///< Soccer
  //for(int i = 0; i < 9; i++)
  //{
  //  if(_codec->_frameNum == selectedFrameList[i])
  //    selectedFrameNum = _codec->_frameNum;
  //}//end for i...

  //short* pDiffLum = NULL;
  //short* pDiffCb  = NULL;
  //short* pDiffCr  = NULL;
  //OverlayMem2Dv2* diffLum	= NULL;
  //OverlayMem2Dv2* diffCb	= NULL;
  //OverlayMem2Dv2* diffCr	= NULL;

  //if(_codec->_frameNum == selectedFrameNum)
  //{
  //  pDiffLum = new short[(_codec->_lumWidth * _codec->_lumHeight) + 2*(_codec->_chrWidth * _codec->_chrHeight)];
  //  pDiffCb = &(pDiffLum[_codec->_lumWidth * _codec->_lumHeight]);
  //  pDiffCr = &(pDiffLum[(_codec->_lumWidth * _codec->_lumHeight) + (_codec->_chrWidth * _codec->_chrHeight)]);

	 // diffLum	= new OverlayMem2Dv2(pDiffLum, _codec->_lumWidth, _codec->_lumHeight, _codec->_lumWidth, _codec->_lumHeight);
	 // diffCb	= new OverlayMem2Dv2(pDiffCb, _codec->_chrWidth, _codec->_chrHeight, _codec->_chrWidth, _codec->_chrHeight);
	 // diffCr	= new OverlayMem2Dv2(pDiffCr, _codec->_chrWidth, _codec->_chrHeight, _codec->_chrWidth, _codec->_chrHeight);

  //  /// Copy the input img into the diff img from where the comp pred img will be subtracted.
  //  _codec->_Lum->SetOverlayDim(_codec->_lumWidth, _codec->_lumHeight);
  //  _codec->_Cb->SetOverlayDim(_codec->_chrWidth, _codec->_chrHeight);
  //  _codec->_Cr->SetOverlayDim(_codec->_chrWidth, _codec->_chrHeight);
  //  _codec->_Lum->SetOrigin(0, 0);
  //  _codec->_Cb->SetOrigin(0, 0);
  //  _codec->_Cr->SetOrigin(0, 0);

  //  diffLum->Write(*_codec->_Lum);
  //  diffCb->Write(*_codec->_Cb);
  //  diffCr->Write(*_codec->_Cr);

  //  /// Restore blk dimensions.
  //  diffLum->SetOverlayDim(16, 16);
  //  diffCb->SetOverlayDim(8, 8);
  //  diffCr->SetOverlayDim(8, 8);
  //  _codec->_Lum->SetOverlayDim(4, 4);
  //  _codec->_Cb->SetOverlayDim(4, 4);
  //  _codec->_Cr->SetOverlayDim(4, 4);
  //}//end if _frameNum...
  /////////////////////////////////////////////////////////////////////////////////////////////

	/// Rip through each macroblock as a linear array and process the
	/// motion vector and each block within the macroblock.
	for (int mb = 0; mb < len; mb++)
	{
		/// Simplify the referencing to the current macroblock.
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);

		///------------------- Motion compensation ------------------------------------------------
		pMb->_mbPartPredMode = MacroBlockH264::Inter_16x16;	///< Fixed at 16x16 for now.

		/// Get the 16x16 motion vector from the motion estimation result list and apply
		/// it to the macroblock. The reference image will then hold the compensated macroblock
		/// to be used as the prediction for calcualting the residual.
		int mvx = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 0);
		int mvy = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 1);
		if (compRef)
			_codec->_pMotionCompensator->Compensate(pMb->_offLumX, pMb->_offLumY, mvx, mvy);

		/////////////////////////////////////////////////////////////////////////////////////////////
		/// Research Data Collection: Mb data capture.
		//if(_codec->_frameNum == selectedFrameNum)
		//{
		   // diffLum->SetOrigin(pMb->_offLumX, pMb->_offLumY);
		   // diffCb->SetOrigin(pMb->_offChrX, pMb->_offChrY);
		   // diffCr->SetOrigin(pMb->_offChrX, pMb->_offChrY);

		//  _codec->_RefLum->SetOverlayDim(16, 16);
		//  _codec->_RefCb->SetOverlayDim(8, 8);
		//  _codec->_RefCr->SetOverlayDim(8, 8);
		   // _codec->_RefLum->SetOrigin(pMb->_offLumX, pMb->_offLumY);
		   // _codec->_RefCb->SetOrigin(pMb->_offChrX, pMb->_offChrY);
		   // _codec->_RefCr->SetOrigin(pMb->_offChrX, pMb->_offChrY);

		//  diffLum->Sub(*_codec->_RefLum);
		//  diffCb->Sub(*_codec->_RefCb);
		//  diffCr->Sub(*_codec->_RefCr);

		//  _codec->_RefLum->SetOverlayDim(4, 4);
		//  _codec->_RefCb->SetOverlayDim(4, 4);
		//  _codec->_RefCr->SetOverlayDim(4, 4);
		//}//end if _frameNum...
		/////////////////////////////////////////////////////////////////////////////////////////////

			/// Store the vector for this macroblock.
		pMb->_mvX[MacroBlockH264::_16x16] = mvx;
		pMb->_mvY[MacroBlockH264::_16x16] = mvy;

		/// Get the predicted motion vector for this macroblock as the median of the neighbourhood 
		/// vectors and subtract it from the macroblock vector.
		int predX, predY;
		MacroBlockH264::GetMbMotionMedianPred(pMb, &predX, &predY);
		pMb->_mvdX[MacroBlockH264::_16x16] = mvx - predX;
		pMb->_mvdY[MacroBlockH264::_16x16] = mvy - predY;

		///------------------- Macroblock processing ----------------------------------------------
		pMb->_mbQP = _codec->_slice._qp;
		_codec->ProcessInterMbImplStd(pMb, addRef, 0);

	}//end for mb...

  /////////////////////////////////////////////////////////////////////////////////////////////
  /// Research Data Collection: Dump to file and clean up in reverse order.

  //if(_codec->_frameNum == selectedFrameNum)
  //{
  //  char* filenameBase = "c:/keithf/Videos/StdTestVideos/Soccer352x288_30fps";
  //  char  sQP[8];
  //  char  sFrmNum[16];
  //  char sFilename[512];
  //  char sFilenameFinal[512];

  //  strcpy(sFilename, (const char*)filenameBase);
  //  strcat(sFilename, (const char*)"_qp");
  //  strcat(sFilename, (const char*)(_itoa(_codec->_slice._qp, sQP, 10)));
  //  strcat(sFilename, (const char*)"_frm");
  //  strcat(sFilename, (const char*)(_itoa(selectedFrameNum, sFrmNum, 10)));

  //  diffLum->SetOverlayDim(_codec->_lumWidth, _codec->_lumHeight);
  //  diffLum->SetOrigin(0, 0);
  //  strcpy(sFilenameFinal, (const char*)sFilename);
  //  strcat(sFilenameFinal, (const char*)"_lum.csv");
  //  diffLum->Dump(diffLum, sFilenameFinal, "");

  //  diffCb->SetOverlayDim(_codec->_chrWidth, _codec->_chrHeight);
  //  diffCb->SetOrigin(0, 0);
  //  strcpy(sFilenameFinal, (const char*)sFilename);
  //  strcat(sFilenameFinal, (const char*)"_cb.csv");
  //  diffLum->Dump(diffCb, sFilenameFinal, "");

  //  diffCr->SetOverlayDim(_codec->_chrWidth, _codec->_chrHeight);
  //  diffCr->SetOrigin(0, 0);
  //  strcpy(sFilenameFinal, (const char*)sFilename);
  //  strcat(sFilenameFinal, (const char*)"_cr.csv");
  //  diffLum->Dump(diffCr, sFilenameFinal, "");
  //}//end if _frameNum...

  //if(diffCr != NULL)
  //  delete diffCr;
  //if(diffCb != NULL)
  //  delete diffCb;
  //if(diffLum != NULL)
  //  delete diffLum;

  //if(pDiffLum != NULL)
  //  delete[] pDiffLum;
  /////////////////////////////////////////////////////////////////////////////////////////////

	*bitsUsed = 0;
	return(1);
}//end InterImgPlaneEncoderImplStdVer1::Encode.

/** The forward and inverse loop of a Std Inter macroblock.
Does NOT include motion prediction and compensation. Assumes these are
already set before called. Code factoring usage. Includes reading the
input image, IT, quantisation, vlc encoding and determining coding
patterns, type, etc. Return the bit cost.
@param pMb		      : Macroblock to operate on.
@param withDR	      : With distortion-rate calculations. If = 2 then distortion only (no rate calc.)
@param usePrevPred  : Use previous prediction mode.
@return				      : Bits consumed or 0 for skipped mb.
*/
int H264v2Codec::ProcessInterMbImplStd(MacroBlockH264* pMb, int addRef, int withDR)
{
	/// NB: _mbQP must be correctly defined before this method is called.
	pMb->_mbEncQP = pMb->_mbQP;  ///< mbQP may be altered in this method therefore store the requested QP.

	if (pMb->_mbQP > H264V2_MAX_QP) ///< Special conditions apply for out of range QP values.
		pMb->_mbQP = H264V2_MAX_QP;

	int distortion = 0;
	int rate = 0;

	int lOffX = pMb->_offLumX;
	int lOffY = pMb->_offLumY;
	int cOffX = pMb->_offChrX;
	int cOffY = pMb->_offChrY;

	/// Base settings for all inter macroblocks.
	pMb->_skip = 0;
	pMb->_intraFlag = 0;

	/// Subtract the ref (compensated) macroblock from the input macroblock and place it in the temp image blocks.

	/// Lum...
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY);   ///< Align the Ref Lum img block with this macroblock.
	_Lum->SetOverlayDim(16, 16);
	_Lum->SetOrigin(lOffX, lOffY);		  ///< Align the Lum img block with this macroblock.
	_16x16->SetOverlayDim(16, 16);
	_16x16->SetOrigin(0, 0);

	_Lum->Read(*(_16x16));				      ///< Read from input Lum into temp.
	_16x16->Sub16x16(*(_RefLum));	      ///< Subtract ref Lum and leave result in temp.

	/// ... and Chr components.
	_RefCb->SetOverlayDim(8, 8);
	_RefCr->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCr->SetOrigin(cOffX, cOffY);
	_Cb->SetOverlayDim(8, 8);
	_Cb->SetOrigin(cOffX, cOffY);
	_Cr->SetOverlayDim(8, 8);
	_Cr->SetOrigin(cOffX, cOffY);
	_8x8_0->SetOverlayDim(8, 8);
	_8x8_0->SetOrigin(0, 0);
	_8x8_1->SetOverlayDim(8, 8);
	_8x8_1->SetOrigin(0, 0);

	_Cb->Read(*(_8x8_0));
	_8x8_0->Sub8x8(*(_RefCb));
	_Cr->Read(*(_8x8_1));
	_8x8_1->Sub8x8(*(_RefCr));

	/// Fill all the non-DC 4x4 blks (Not blks = -1, 17, 18) of the macroblock blocks with 
	/// the residual image colour components (after motion compensation/prediction).
	MacroBlockH264::LoadBlks(pMb, _16x16, 0, 0, _8x8_0, _8x8_1, 0, 0);

	/// ------------------ Transform & Quantisation --------------------------------------------
	if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
		TransAndQuantInter16x16MBlk(pMb);

	/// ------------------- Zero Coeffs for QP > H264V2_MAX_QP -------------------------------------
	if (pMb->_mbEncQP > H264V2_MAX_QP)
		CoeffZeroingQuantisationVer2(pMb);
	/*
	  {
		int blk, i;
		const int *pZZ4x4 = CAVLCH264Impl::zigZag4x4Pos;
		const int *pZZ2x2 = CAVLCH264Impl::zigZag2x2Pos;

		/// There are 16 (4x4) AC coeffs per blk and the _mbEncQP value indicates how many of
		/// these are to be zeroed in reverse zig-zag order. _mbEncQP = {52..66} implies that
		/// {1..15} AC coeffs are zeroed. {67..70} Chr DC 2x2 coeffs additionally zeroed. {71}
		/// Lum DC coeffs additionally zeroed.
		int acCoeffZeroed     = 15; ///< By default.
		int chrDcCoeffZeroed  = 0;
		if( pMb->_mbEncQP <= (H264V2_MAX_QP + 15) )
		  acCoeffZeroed = pMb->_mbEncQP - H264V2_MAX_QP;
		else if( pMb->_mbEncQP <= 70 )  ///< Include Chr DC coeffs for QP = {67..70}.
		{
		  chrDcCoeffZeroed = pMb->_mbEncQP - 66;
		}//end else if _mbEncQP...
		else ///< if( pMb->_mbEncQP == 71 )  Include Lum and Chr DC coeffs.
		{
		  acCoeffZeroed     = 16;
		  chrDcCoeffZeroed  = 4;
		}//end else...
		int startPos4x4 = 16 - acCoeffZeroed;
		int startPos2x2 = 4 - chrDcCoeffZeroed;

		/// Zero coeff in the mb according to the QP rules above.
		  for(blk = MBH264_LUM_0_0; blk < MBH264_NUM_BLKS; blk++)
		  {
			  BlockH264* pBlk = pMb->_blkParam[blk].pBlk;
		  short* pB = pBlk->GetBlk();

		  if( (blk != MBH264_CB_DC) && (blk != MBH264_CR_DC) )  ///< Exclude Chr DC blks.
		  {
			for(i = startPos4x4; i < 16; i++)
			{
			  if( pB[pZZ4x4[i]] )  ///< Only need to change coeffs that are not already zero.
				pB[pZZ4x4[i]] = 0;
			}//end for i...
		  }//end if blk...
		  else  ///< Chr DC blks AND zero'ing required.
		  {
			if(chrDcCoeffZeroed)
			{
			  for(i = startPos2x2; i < 4; i++)
			  {
				if( pB[pZZ2x2[i]] )  ///< Only need to change coeffs that are not already zero.
				  pB[pZZ2x2[i]] = 0;
			  }//end for i...
			}//end if chrDcCoeffZeroed...
		  }//end else...

		  }//end for blk...

	  }//end if _mbEncQP...
	*/
	/// ------------------ Set patterns and type -----------------------------------------------
	/// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	/// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the delta quantisation parameter. Note that it is not coded onto the bit stream
	/// if all 4x4 blocks have no coeffs (not coded). The quant param for this macroblock is then
	/// assumed to be the same as the previous non-skipped macroblock within the same slice. This
	/// works because multiplying any quant param by zero is still zero. NB: This alters the _mbQP
  /// value therefore the original QP used for the encoding is stored in _mbEncQP.
	pMb->_mb_qp_delta = GetDeltaQP(pMb);

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// Only Inter_16x16 mode is currently implemented so only 1 motion vector is 
	/// checked for the macroblock skip mode.
	if (pMb->_coded_blk_pattern == 0)
	{
		/// First set of conditions for skip are dependent on a zero 16x16 single motion vector.
		if (MacroBlockH264::SkippedZeroMotionPredCondition(pMb))
		{
			if ((pMb->_mvX[MacroBlockH264::_16x16] == 0) && (pMb->_mvY[MacroBlockH264::_16x16] == 0))
				pMb->_skip = 1;
		}//end if SkippedZeroMotionPredCondition...
		/// Second condition is dependent on the previously calculated median difference motion vector.
		else
		{
			if ((pMb->_mvdX[MacroBlockH264::_16x16] == 0) && (pMb->_mvdY[MacroBlockH264::_16x16] == 0))
				pMb->_skip = 1;
		}//end else...
	}//end _coded_blk_pattern...

	  /// ------------------ Calc distortion from feedback loop ----------------------------------
	  /// Implement the feedback loop into the ref image. The block coeffs are still required for 
	  /// the context-aware vlc coding and therefore the temporary working blocks within the macroblock 
	  /// class are used for this feedback loop.
	if (pMb->_coded_blk_pattern)
	{
		/// --------------------- Inverse Transform & Quantisation -------------------------------
		if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
			InverseTransAndQuantInter16x16MBlk(pMb, 1);

		/// --------------------- Image Storing into Ref -----------------------------------------
		/// Fill the temp image (difference) colour components from all the non-DC 4x4 
		/// blks (i.e. Not blks = -1, 17, 18) of the macroblock temp blocks. 
		MacroBlockH264::StoreBlks(pMb, _16x16, 0, 0, _8x8_0, _8x8_1, 0, 0, 1);

		/// --------------------- Add the prediction ---------------------------------------------
		/// Lum.
		_RefLum->SetOverlayDim(16, 16);
		_RefLum->SetOrigin(lOffX, lOffY);			///< Align the Ref Lum img block with this macroblock.
		_16x16->SetOverlayDim(16, 16);
		_16x16->SetOrigin(0, 0);
		_16x16->Add16x16WithClip255(*(_RefLum));            ///< Add ref Lum and leave result in temp img.
		if (addRef)
			_RefLum->Write16x16(*(_16x16));

		/// Cb.
		_RefCb->SetOverlayDim(8, 8);
		_RefCb->SetOrigin(cOffX, cOffY);
		_8x8_0->SetOverlayDim(8, 8);
		_8x8_0->SetOrigin(0, 0);
		_8x8_0->Add8x8WithClip255(*(_RefCb));
		if (addRef)
			_RefCb->Write8x8(*(_8x8_0));
		/// Cr.
		_RefCr->SetOverlayDim(8, 8);
		_RefCr->SetOrigin(cOffX, cOffY);
		_8x8_1->SetOverlayDim(8, 8);
		_8x8_1->SetOrigin(0, 0);
		_8x8_1->Add8x8WithClip255(*(_RefCr));
		if (addRef)
			_RefCr->Write8x8(*(_8x8_1));

	}//end if _coded_blk_pattern...

	/// ------------------ Calc the Distortion & Rate and store results ----------------------------------
	/// Store distortion for this quant value.
	if (withDR)
	{
		/// Calc distortion with Input. If there was a coded blk pattern then compare with temp img else
	  /// with the unaltered ref img.
		OverlayMem2Dv2* pLI = _RefLum;
		OverlayMem2Dv2* pCbI = _RefCb;
		OverlayMem2Dv2* pCrI = _RefCr;
		if (pMb->_coded_blk_pattern)
		{
			pLI = _16x16;
			pCbI = _8x8_0;
			pCrI = _8x8_1;
		}//end else if _coded_blk_pattern...

#ifdef USE_ABSOLUTE_DIFFERENCE
		distortion += pLI->Tad16x16(*_Lum);	///< Total abs distortion between Ref and input Lum.
		distortion += pCbI->Tad8x8(*_Cb);
		distortion += pCrI->Tad8x8(*_Cr);
#else
		distortion += pLI->Tsd16x16(*_Lum);	///< Total square distortion between Ref and input Lum.
		distortion += pCbI->Tsd8x8(*_Cb);
		distortion += pCrI->Tsd8x8(*_Cr);
#endif
    /// Modify the distortion with the region of interest map.
    distortion = ROIDistortion(pMb->_mbIndex, distortion);
		pMb->_distortion[pMb->_mbEncQP] = distortion;

		//    if(!pMb->_skip)
		//		  rate = MacroBlockLayerBitCounter(pMb);
		//    else
		if (pMb->_skip)
		{
			/// Ensure coeffs settings are synchronised for future use by neighbours.
			for (int i = 1; i < MBH264_NUM_BLKS; i++)
				pMb->_blkParam[i].pBlk->SetNumCoeffs(0);
		}//end if _skip...

	//    pMb->_rate[pMb->_mbEncQP] = rate;
	}//end if withDR...

	/// Store rate for this quant value if required.
	if ((withDR == 1) && (!pMb->_skip)) ///< For withDR = 2, no rate calculation is done.
	{
		rate = MacroBlockLayerBitCounter(pMb);
		pMb->_rate[pMb->_mbEncQP] = rate;
	}//end if withDR...

	return(rate);
}//end ProcessInterMbImplStd.

/** The forward and inverse loop of a Std Inter macroblock with a Dmax criterion.
Does NOT include motion prediction and compensation. Assumes these are
already set before called. Code factoring usage. Includes reading the
input image, IT, quantisation, vlc encoding and determining coding
patterns, type, etc. Includes accumulation of the frame MAD measure.
Return the bit cost.
@param pMb		      : Macroblock to operate on.
@param withDR	      : With distortion-rate calculations. If = 2 then distortion only (no rate calc.)
@param Dmax         : Max distortion for this mb.
@param minQP        : Lower limit for the QP descent.
@return				      : Bits consumed or 0 for skipped mb.
*/
int H264v2Codec::ProcessInterMbImplStd(MacroBlockH264* pMb, int addRef, int withDR, int Dmax, int minQP)
{
	/// NB: _mbQP must be correctly defined before this method is called.
	pMb->_mbEncQP = pMb->_mbQP;  ///< mbQP may be altered in this method therefore store the requested QP.

	if (pMb->_mbQP > H264V2_MAX_QP) ///< Special conditions apply for out of range QP values.
		pMb->_mbQP = H264V2_MAX_QP;

	/// The delta QP for a mb is constrained to {-26...25}. 
	int q = pMb->_mbQP;

	int prevQP = GetPrevMbQP(pMb);
	int lowQP = prevQP - 26;
	if (lowQP < minQP) lowQP = minQP;
	int highQP = prevQP + 25;
	if (highQP > H264V2_MAX_QP) highQP = H264V2_MAX_QP;
	if (q < lowQP) q = lowQP;
	else if (q > highQP) q = highQP;
	pMb->_mbQP = q;

	int distortion = 0;
	int rate = 0;

	int lOffX = pMb->_offLumX;
	int lOffY = pMb->_offLumY;
	int cOffX = pMb->_offChrX;
	int cOffY = pMb->_offChrY;

	/// Base settings for all inter macroblocks.
	pMb->_skip = 0;
	pMb->_intraFlag = 0;

	/// Subtract the ref (compensated) macroblock from the input macroblock and place it in the temp image blocks.

	/// Lum...
	_RefLum->SetOverlayDim(16, 16);
	_RefLum->SetOrigin(lOffX, lOffY);   ///< Align the Ref Lum img block with this macroblock.
	_Lum->SetOverlayDim(16, 16);
	_Lum->SetOrigin(lOffX, lOffY);		  ///< Align the Lum img block with this macroblock.
	_16x16->SetOverlayDim(16, 16);
	_16x16->SetOrigin(0, 0);

	_Lum->Read(*(_16x16));				      ///< Read from input Lum into temp.
	_16x16->Sub16x16(*(_RefLum));	      ///< Subtract ref Lum and leave result in temp.

  /// Frame luma signal MAD and MSD accumulation. These variables were cleared before processing the frame.
  /// The calculation is only defined within the mb and from col 1 row 0 onwards, the first pel in each row
  /// is then compared with the one above.
	if (withDR)
	{
		for (int r = 0; r < 16; r++)
			for (int c = 0; c < 16; c++)
			{
				if (r || c)  ///< Not (0,0)
				{
					int curr, prev;
					curr = _16x16->Read(c, r);
					if (c)
						prev = _16x16->Read(c - 1, r); ///< Left.
					else
						prev = _16x16->Read(c, r - 1); ///< Above.

					_frameMAD += DISTORTIONABSDIFF(curr, prev);
					_frameMSD += DISTORTIONSQRDIFF(curr, prev);
				}//end if r...
			}//end for r & c...

		_frameMAD_N += 255;
	}//end if withDR...

	  /// ... and Chr components.
	_RefCb->SetOverlayDim(8, 8);
	_RefCr->SetOverlayDim(8, 8);
	_RefCb->SetOrigin(cOffX, cOffY);
	_RefCr->SetOrigin(cOffX, cOffY);
	_Cb->SetOverlayDim(8, 8);
	_Cb->SetOrigin(cOffX, cOffY);
	_Cr->SetOverlayDim(8, 8);
	_Cr->SetOrigin(cOffX, cOffY);
	_8x8_0->SetOverlayDim(8, 8);
	_8x8_0->SetOrigin(0, 0);
	_8x8_1->SetOverlayDim(8, 8);
	_8x8_1->SetOrigin(0, 0);

	_Cb->Read(*(_8x8_0));
	_8x8_0->Sub8x8(*(_RefCb));
	_Cr->Read(*(_8x8_1));
	_8x8_1->Sub8x8(*(_RefCr));

	/// Fill all the non-DC 4x4 blks (Not blks = -1, 17, 18) of the macroblock blocks with 
	/// the residual image colour components (after motion compensation/prediction).
	MacroBlockH264::LoadBlks(pMb, _16x16, 0, 0, _8x8_0, _8x8_1, 0, 0);

	/// ------------------ Transform & Quantisation --------------------------------------------
  /// Includes the distortion calc and the inverse quant and transform to the temp blks. For 
  /// extended range QP > H264V2_MAX_QP the appropriate coeffs are zeroed. 
	if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
		pMb->_distortion[pMb->_mbEncQP] = TransAndQuantInter16x16MBlk(pMb, Dmax, lowQP);

	/// ------------------ Set patterns and type -----------------------------------------------
	/// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	/// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the delta quantisation parameter. Note that it is not coded onto the bit stream
	/// if all 4x4 blocks have no coeffs (not coded). The quant param for this macroblock is then
	/// assumed to be the same as the previous non-skipped macroblock within the same slice. This
	/// works because multiplying any quant param by zero is still zero. NB: This alters the _mbQP
  /// value therefore the original QP used for the encoding is stored in _mbEncQP.
	pMb->_mb_qp_delta = GetDeltaQP(pMb);

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// Only Inter_16x16 mode is currently implemented so only 1 motion vector is 
	/// checked for the macroblock skip mode.
	if (pMb->_coded_blk_pattern == 0)
	{
		/// First set of conditions for skip are dependent on a zero 16x16 single motion vector.
		if (MacroBlockH264::SkippedZeroMotionPredCondition(pMb))
		{
			if ((pMb->_mvX[MacroBlockH264::_16x16] == 0) && (pMb->_mvY[MacroBlockH264::_16x16] == 0))
				pMb->_skip = 1;
		}//end if SkippedZeroMotionPredCondition...
		/// Second condition is dependent on the previously calculated median difference motion vector.
		else
		{
			if ((pMb->_mvdX[MacroBlockH264::_16x16] == 0) && (pMb->_mvdY[MacroBlockH264::_16x16] == 0))
				pMb->_skip = 1;
		}//end else...
	}//end _coded_blk_pattern...

	  /// ------------------ Calc distortion from feedback loop ----------------------------------
	  /// Implement the feedback loop into the ref image. The block coeffs are still required for 
	  /// the context-aware vlc coding and therefore the temporary working blocks within the macroblock 
	  /// class are used for this feedback loop.
	if (pMb->_coded_blk_pattern)
	{
		/// --------------------- Inverse Transform & Quantisation -------------------------------
	/// The inverse to the temp blks was included in the TransAndQuantInter16x16MBlk() method above.

		/// --------------------- Image Storing into Ref -----------------------------------------
		/// Fill the temp image (difference) colour components from all the non-DC 4x4 
		/// blks (i.e. Not blks = -1, 17, 18) of the macroblock temp blocks. 
		MacroBlockH264::StoreBlks(pMb, _16x16, 0, 0, _8x8_0, _8x8_1, 0, 0, 1);

		/// --------------------- Add the prediction ---------------------------------------------
		/// Lum.
		_RefLum->SetOverlayDim(16, 16);
		_RefLum->SetOrigin(lOffX, lOffY);			    ///< Align the Ref Lum img block with this macroblock.
		_16x16->SetOverlayDim(16, 16);
		_16x16->SetOrigin(0, 0);
		_16x16->Add16x16WithClip255(*(_RefLum));  ///< Add ref Lum and leave result in temp img.
		if (addRef)
			_RefLum->Write16x16(*(_16x16));

		/// Cb.
		_RefCb->SetOverlayDim(8, 8);
		_RefCb->SetOrigin(cOffX, cOffY);
		_8x8_0->SetOverlayDim(8, 8);
		_8x8_0->SetOrigin(0, 0);
		_8x8_0->Add8x8WithClip255(*(_RefCb));
		if (addRef)
			_RefCb->Write8x8(*(_8x8_0));
		/// Cr.
		_RefCr->SetOverlayDim(8, 8);
		_RefCr->SetOrigin(cOffX, cOffY);
		_8x8_1->SetOverlayDim(8, 8);
		_8x8_1->SetOrigin(0, 0);
		_8x8_1->Add8x8WithClip255(*(_RefCr));
		if (addRef)
			_RefCr->Write8x8(*(_8x8_1));

	}//end if _coded_blk_pattern...

	/// ------------------ Calc the rate and store results ----------------------------------
	if (withDR && pMb->_skip)
	{
		/// Ensure coeffs settings are synchronised for future use by mb neighbours.
		for (int i = 1; i < MBH264_NUM_BLKS; i++)
			pMb->_blkParam[i].pBlk->SetNumCoeffs(0);
	}//end if withDR...

	/// Store rate for this quant value if required.
	if (!pMb->_skip)
	{
		if ((withDR == 1) || (withDR == 3)) ///< For withDR = 2, no rate calculation is done.
		{
			rate = MacroBlockLayerCoeffBitCounter(pMb);
			pMb->_rate[pMb->_mbEncQP] = rate;
		}//end if withDR...
	}//end if !skip...

	return(rate);
}//end ProcessInterMbImplStd.

/// Does NOT include motion prediction and compensation. Assumes these mb members are already set before called.
int H264v2Codec::ProcessInterMbImplStdMin(MacroBlockH264* pMb)
{
	/// Set the difference to the compensated reference to zero and therefore no operation needs to be
	/// performed on the ref. Zero and clear all blocks.

	  /// NB: _mbQP must be correctly defined before this method is called.
	int rate = 0;

	/// Base settings for all inter macroblocks.
	pMb->_intraFlag = 0;
	pMb->_skip = 0;

	/// Clear the delta quantisation parameter and set the _mbQP to the same as the previous non-skipped mb. The
  /// _mbQP does not have any impact because all coeffs are set to zero.
	pMb->_mb_qp_delta = 0;
	/// Find the previous non-skipped macroblock.
	int prevMbIdx = pMb->_mbIndex - 1;
	/// Re-align the quantisation param for future delta qp calculations.
	if (prevMbIdx >= 0)
	{
		if (pMb->_slice == _pMb[prevMbIdx]._slice)	/// Previous macroblock within same slice.
			pMb->_mbQP = _pMb[prevMbIdx]._mbQP;
		else
			pMb->_mbQP = _slice._qp;
	}//end if prevMbIdx...
	else
		pMb->_mbQP = _slice._qp;

	/// Force all values to zero before encoding.
	_16x16->SetOverlayDim(16, 16);
	_16x16->SetOrigin(0, 0);
	_16x16->Clear();
	_8x8_0->SetOverlayDim(8, 8);
	_8x8_0->SetOrigin(0, 0);
	_8x8_0->Clear();
	_8x8_1->SetOverlayDim(8, 8);
	_8x8_1->SetOrigin(0, 0);
	_8x8_1->Clear();

	/// Fill all the non-DC 4x4 blks (Not blks = -1, 17, 18) of the macroblock blocks with 
	/// the residual image colour components (after motion compensation/prediction).
	MacroBlockH264::LoadBlks(pMb, _16x16, 0, 0, _8x8_0, _8x8_1, 0, 0);

	/// ------------------ Transform & Quantisation --------------------------------------------
	if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
		TransAndQuantInter16x16MBlk(pMb);

	/// ------------------ Set patterns and type -----------------------------------------------
	/// Determine the coded Lum and Chr patterns. The _codedBlkPatternLum, _codedBlkPatternChr 
	/// and _coded_blk_pattern members are set.
	MacroBlockH264::SetCodedBlockPattern(pMb);

	/// Determine the macroblock type. From the prediction modes and patterns set the 
	/// _mb_type member.
	MacroBlockH264::SetType(pMb, _slice._type);

	/// Only Inter_16x16 mode is currently implemented so only 1 motion vector is 
	/// checked for the macroblock skip mode. If motion is zero then mb is skipped.
	if (pMb->_coded_blk_pattern == 0)
	{
		/// First set of conditions for skip are dependent on a zero 16x16 single motion vector.
		if (MacroBlockH264::SkippedZeroMotionPredCondition(pMb))
		{
			if ((pMb->_mvX[MacroBlockH264::_16x16] == 0) && (pMb->_mvY[MacroBlockH264::_16x16] == 0))
				pMb->_skip = 1;
		}//end if SkippedZeroMotionPredCondition...
		/// Second condition is dependent on the previously calculated median difference motion vector.
		else
		{
			if ((pMb->_mvdX[MacroBlockH264::_16x16] == 0) && (pMb->_mvdY[MacroBlockH264::_16x16] == 0))
				pMb->_skip = 1;
		}//end else...
	}//end _coded_blk_pattern...

	if (!pMb->_skip)
		rate = MacroBlockLayerBitCounter(pMb);
	else
	{
		/// Ensure coeffs settings are synchronised for future use by neighbours.
		for (int i = 1; i < MBH264_NUM_BLKS; i++)
			pMb->_blkParam[i].pBlk->SetNumCoeffs(0);
	}//end else...

	pMb->_rate[0] = rate;

	return(rate);
}//end ProcessInterMbImplStdMin.

/** Calc delta QP based on macroblock QP.
The coded block pattern, _intraFlag and _mbQP values must be correctly set before calling this
method. The _skip mode for P mbs alters the _mbQP value.
@param pMb    : Macroblock to operate on.
@return       : Delta QP for these mb parameters.
*/
int H264v2Codec::GetDeltaQP(MacroBlockH264* pMb)
{
	int deltaQP = 0;

	/// Find the previous non-skipped macroblock. For Intra slices no previous macroblocks are skipped.
	int prevMbIdx = pMb->_mbIndex - 1;

	if (pMb->_coded_blk_pattern || pMb->_intraFlag)
	{
		if (prevMbIdx >= 0)	///< Previous macroblock is within the image boundaries.
		{
			if (pMb->_slice == _pMb[prevMbIdx]._slice)	/// Previous macroblock within same slice.
				deltaQP = pMb->_mbQP - _pMb[prevMbIdx]._mbQP;
			else
				deltaQP = pMb->_mbQP - _slice._qp;
		}//end if prevMbIdx...
		else
			deltaQP = pMb->_mbQP - _slice._qp;
	}//end if _coded_blk_pattern...
	else
	{
		/// Re-align the quantisation param for future delta qp calculations.
		if (prevMbIdx >= 0)
		{
			if (pMb->_slice == _pMb[prevMbIdx]._slice)	/// Previous macroblock within same slice.
				pMb->_mbQP = _pMb[prevMbIdx]._mbQP;
			else
				pMb->_mbQP = _slice._qp;
		}//end if prevMbIdx...
		else
			pMb->_mbQP = _slice._qp;
	}//end else...

	return(deltaQP);
}//end GetDeltaQP.

/** Get the previous macroblock QP value.
Picture and slice boundaries are considered in returning the
neighbouring mb QP value.
@param pMb    : Macroblock to operate on.
@return       : Previous Mb QP.
*/
int H264v2Codec::GetPrevMbQP(MacroBlockH264* pMb)
{
	int prevMbIdx = pMb->_mbIndex - 1;

	///< Previous macroblock is within the image boundaries and within same slice.
	if ((prevMbIdx >= 0) && (pMb->_slice == _pMb[prevMbIdx]._slice))
		return(_pMb[prevMbIdx]._mbQP);
	else
		return(_slice._qp);
}//end GetPrevMbQP.

int H264v2Codec::GetPrevMbEncQP(MacroBlockH264* pMb)
{
  int prevMbIdx = pMb->_mbIndex - 1;

  ///< Previous macroblock is within the image boundaries and within same slice.
  if ((prevMbIdx >= 0) && (pMb->_slice == _pMb[prevMbIdx]._slice))
    return(_pMb[prevMbIdx]._mbEncQP);
  else
    return(_slice._qp);
}//end GetPrevMbQP.


/** Get the next macroblock QP value.
Picture and slice boundaries are considered in returning the
neighbouring mb QP value.
@param pMb    : Macroblock to operate on.
@return       : Next Mb QP.
*/
int H264v2Codec::GetNextMbQP(MacroBlockH264* pMb)
{
	int nextMbIdx = pMb->_mbIndex + 1;

	///< Previous macroblock is within the image boundaries and within same slice.
	if ((nextMbIdx < _mbLength) && (pMb->_slice == _pMb[nextMbIdx]._slice))
		return(_pMb[nextMbIdx]._mbQP);
	else
		return(pMb->_mbIndex);
}//end GetPrevMbQP.

/** Encode the image data for Inter pictures with QP adaptation at macroblock level.
Process all macroblocks in order and only write the result to the ref image
space if the writeRef code is set. Bit 1 of writeRef refers to motion compensation
and bit 0 to adding the difference to the ref. The motion estimation process with
its associated data structures is assumed to have been completed before this
method is called. The macroblock obj is prepared for coding onto the bit stream.
Note: For iteratively calling this method;
	Call 1:			writeRef = 2 (compensation to ref but no adding).
	Call 2 + X: writeRef = 0 (ref is motion compensated already from call 1).
	Call final:	writeRef = 1 (allow final add to ref).
The macroblock wide _mbPQ is adapted to ensure that the resulting stream will use
no more than allowedBits. This algorithm uses a bisection technique to minimise the
max distortion by adapting the macroblock quant value until the nearest rate to the
total allowable bits is achieved.
@param allowedBits	: Total remaining bits to target.
@param bitsUsed			: Num of bits used for this encoding.
@param writeRef			: Bit code 1x/0x = do/do not comp. x1/x0 do/do not add ref.
@return							: 1 = success, 0 = error.
*/
int H264v2Codec::InterImgPlaneEncoderImplMinMax::Encode(int allowedBits, int* bitsUsed, int writeRef)
{
	int mb, bitCount;
	int compRef = writeRef & 2;
	int addRef = writeRef & 1;
	int len = _codec->_mbLength;
	int minMBIndex = len; ///< Mark the macroblock from where min units must be encoded.

	/// Set a bound on the max quality by choosing a min qp value that the MinMax search is limited to.
	int qEnd = _codec->_minQPInter;

	/// Slice without partitioning and therefore only one set of slice parameters.

	/// Set up the input and ref image mem overlays.
	_codec->_Lum->SetOverlayDim(4, 4);
	_codec->_Cb->SetOverlayDim(4, 4);
	_codec->_Cr->SetOverlayDim(4, 4);
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOverlayDim(8, 8);

	/// All integer transforms are Inter in this method.
	_codec->_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 0);
	_codec->_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TChr->SetParameter(IForwardTransform::INTRA_FLAG_ID, 0);
	/// By default the DC transforms were set in the TransformOnly mode in the Open() method.

  /// Mark this time point for later use.
	int preMotionTime = 0;
	if (_codec->_timeLimitMs)
		preMotionTime = (int)_codec->GetCounter();

	///	---------------------- Motion Vector encoding ---------------------------------------

	/// In this implementation QP adaptation is performed after full motion compensation.
	/// Get the motion vector list to work with. Assume SIMPLE2D type list.
	int	listLen = _codec->_pMotionEstimationResult->GetLength();
	if (listLen != len)
	{
		*bitsUsed = 0;
		return(0);	///< Error: Motion vector list must match.
	}//end if listLen...

	/// Prepare the compensation on a per macroblock basis.
	if (compRef)
		_codec->_pMotionCompensator->PrepareForSingleVectorMode();

	/// Count the bits used for the motion vectors by iterating through the
	/// macroblocks and encoding the differential motion vector diff (_mvdX,_mvdY). 
  /// Check that there are sufficient bits available for each vector. If not then 
  /// truncate the estimation result and mark all remaining macroblocks as skipped. 
  /// Store the bit cost of the mb with only motion vectors coded in _rate[0].
	int bitCost = 0;
	int mbSkipRun = 0;
	/// Start with all mbs skipped to the end by counting bits for mbSkipRun = len;
	int minPictureBitsToEnd = _codec->_pHeaderUnsignedVlcEnc->Encode(len);
	for (mb = 0; mb < len; mb++)
	{
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.

		/// Set the mb with the the motion vector, do the prediction to get the MVD and apply the motion compensation.
		pMb->_mbPartPredMode = MacroBlockH264::Inter_16x16; ///< Currently only 16x16 mode supported.
		/// Extract the 16x16 vector from the motion estimation result list.
		int mvx = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 0);
		int mvy = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 1);
		/// Store the motion vector in the mb.
		pMb->_mvX[MacroBlockH264::_16x16] = mvx;
		pMb->_mvY[MacroBlockH264::_16x16] = mvy;
		/// Get the predicted motion vector for this mb as the median of the neighbourhood vectors and subtract from the mb vector.
		int predX, predY;
		MacroBlockH264::GetMbMotionMedianPred(pMb, &predX, &predY);
		pMb->_mvdX[MacroBlockH264::_16x16] = mvx - predX;
		pMb->_mvdY[MacroBlockH264::_16x16] = mvy - predY;

		/// Motion compensate the macroblock.
		if (compRef)
			_codec->_pMotionCompensator->Compensate(pMb->_offLumX, pMb->_offLumY, mvx, mvy);

		int lclAllowedBits = (allowedBits - bitCost) - minPictureBitsToEnd;	///< So far before encoding this MVD pair.

	  /// Process the compensated mb as motion vector only with zero'ed residual to measure its rate cost.
		int lclBitCost = _codec->ProcessInterMbImplStdMin(pMb);

		/// For skipped mbs, the previous skipped bit count must be included in the minPictureBitsToEnd.
		if (!pMb->_skip)
		{
			/// Sum the run and coding bit contributions.
			lclBitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

			/// Are there enough bits left to encode this run + MVD pair?
			if (lclBitCost < lclAllowedBits)
				bitCost += lclBitCost;  ///< Update the accumulator.
			else	///< Truncation condition.
			{
				/// Clean up all remaining macroblocks as skipped and exit. In this implementation
				/// when there are not enough bits to encode all the motion vectors the estimated list
				/// is truncated with [0,0] difference vectors or [0,0] motion with special conditions 
			  /// and re-compensated.
				return(DamageControlMvOnly(allowedBits, bitsUsed));
			}//end else...

			mbSkipRun = 0; ///< Reset.

		}//end if !_skip...
		else  ///< Skipped.
			mbSkipRun++;

		/// Update remaining bits for next mb where all remaining mbs are skipped.
		int mbSkippedToEnd = mbSkipRun + ((len - 1) - mb);
		if (mbSkippedToEnd)
			minPictureBitsToEnd = _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkippedToEnd);
		else
			minPictureBitsToEnd = 0;
	}//end for mb...
  /// Include the last run to the end.
	bitCost += minPictureBitsToEnd;
	if (bitCost >= allowedBits)  ///< ...and check.
		return(DamageControlMvOnly(allowedBits, bitsUsed));
	int minMvBitsToEnd = bitCost;	///< Hold the total motion vector with zero residual bit cost.

	/*
	-------------------------------------------------------------------------------------------
	Residual macroblock Q search.
	-------------------------------------------------------------------------------------------
	*/
	/// Initialisation step: 
	  ///		lower(D,R) : QP = H264V2_MAX_QP.
	  /// Note that the D values represent the worst distortion of only one macroblock in the entire
	  /// frame but the R values represent the accumulated rate of all macroblocks.
	int	Dl = 0;
	int	Du = 0;
	int	Rl = 0;
	int	Ru = 0;
	int	R = 0;
	int	D = 0;
	int	Dmax = 0;
	int iterations = 0;
	int	invalidated = 0;
	int mbDmax = 0;

	/// Test QP = H264V2_MAX_QP for all macroblocks and determine if this min possible rate is, at the very 
	/// least, less than the target rate. Damage control is required if not, where minimal encodings
	/// are attempted for the macroblocks. Otherwise, proceed with the initialisation.
	mbSkipRun = 0;
	for (mb = 0; mb < len; mb++)
	{
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.

		/// Note that the 1st mb will have a delta offset from _slice._qp to get it to _mbQP = H264V2_MAX_QP.
		pMb->_mbQP = H264V2_MAX_QP;
		/// Include all macroblocks at the start.
		pMb->_include = 1;
		_pQl[mb] = H264V2_MAX_QP;
		_pQ[mb] = H264V2_MAX_QP;	///< Active quantiser vector is always the lower (max distortion) side.

		/// Accumulate the rate and find the largest distortion for all mbs. Call returns 0 for skipped mbs. The ref
	  /// holding the compensated motion is not updated with the encoding in this traversal of the mbs.
		Rl += _codec->ProcessInterMbImplStd(pMb, 0, 1);
		if (!pMb->_skip)
		{
			/// Sum of skip run and coded mb bits accumulated.
			Rl += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
			mbSkipRun = 0;
		}//end if !_skip...
		else
			mbSkipRun++;

		if (pMb->_distortion[H264V2_MAX_QP] > Dl)
		{
			Dl = pMb->_distortion[H264V2_MAX_QP];
			mbDmax = mb;	///< Mark the mb index with the peak distortion.
		}//end if _distortion...

		/// No early exit because damage control uses this result = Rl.

	}//end for mb...
  /// Add last skip run.
	if (mbSkipRun)
		Rl += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

	/// Set the limits on the number of optimisation iterations by a timer or iteration number. Take into
  /// account the time taken to get to this point from _startTimer but exclude the motion estimation time
  /// as an offset.
	int start = 0;
	int timeOffset = 0;
	int lclIterations = _codec->_interIterations;
	int lclTimeLimit = _codec->_timeLimitMs;  ///< In ms. A zero indicates either no counter available or not set.
	if (lclTimeLimit)
	{
		start = (int)_codec->GetCounter();
		timeOffset = 2 * (preMotionTime - _codec->_startTime);
	}//end if lclTimeLimit...

	  /// Test that there is room to adapt from quant param = H264V2_MAX_QP, else do damage control.
	if (Rl <= allowedBits)
	{

		/// Create an epsilon at 0.4% of the available bits with a min limit of 16 bits.
		/// Adjust the available bits to be epsilon below allowedBits.
		int closeEnough = allowedBits / 250;	///< 0.4%
		if (closeEnough < 16)
			closeEnough = 16;
		int closeEnoughDist = 8;	///< Sqr error per macroblock.

	  /// Model based faster algorithm to improve on the bi-section method.

		/// Modify the target rate to a close offset on the low rate side.
		int bitTarget = allowedBits - closeEnough;

		/// The worst case at QP = H264V2_MAX_QP uses less bits than allowed so there is some
		/// room to manouver and we can proceed. The upper point is initially not calculated
		/// directly but will hold a valid value during the algorithm convergence after the
		/// first iteration.
		/// The upper point is initiallised to some wild guess.
		Du = 1;									///< Min (im)possible.
		Ru = allowedBits << 8;	///< Some very large number.

		/// Initialisation is complete and the adapting bi-section/power model algorithm starts 
		/// here. The stopping criteria are 1) a rate less than, but close to, the allowed bits
		/// or 2) small change in distortion when reducing the interval.
		int done = 0;
		while (!done)
		{
			int prevDmax = Dmax;

			/// Make a prediction assuming a power law model.
			Dmax = _codec->FitDistPowerModel(Rl, Dl, Ru, Du, bitTarget);
			/// Only use this prediction if it is bounded by (Rl,Dl) and (Ru,Du).
			if ((Dmax < Du) || (Dmax > Dl))	///< Out of bound.
				Dmax = _codec->FitDistLinearModel(Rl, Dl, Ru, Du, bitTarget);

			/// Encourage the descent direction by adding a decreasing factor to the
			/// predicted Dmax such that convergence to Dmax is from the (Rl,Dl) side.
			Dmax += abs(Dl - Dmax) / 4;

			if ((Dmax < Du) || (Dmax > Dl) || (Dmax == prevDmax))	///< Still out of bound.
				Dmax = ((Du + Dl) + 1) >> 1;	///< Set the midpoint max distortion.

			/// At each macroblock reduce the quant value until the distortion is lower
			/// than Dmax. pQ[] must always hold the lower rate (smaller valued) quant vector 
			/// as the previous best choice.
			R = 0;
			int firstMbChange = len;
			if (invalidated)	///< Frame encoded coeffs must be invalidated if _pQ[] does not represent the actual QPs used.
				firstMbChange = 0;
			mbSkipRun = 0;
			for (mb = 0; mb < len; mb++)
			{
				MacroBlockH264* pMb = &(_codec->_pMb[mb]);

				/// Record the found QP where the macroblock dist is just below Dmax and accumulate the rate for this macroblock.
				_pQ[mb] = _codec->GetMbQPBelowDmaxVer2(*pMb, _pQ[mb], Dmax, &firstMbChange, qEnd, false);
				R += pMb->_rate[_pQ[mb]]; ///< Rate = 0 for skipped mbs.
				if (!pMb->_skip)
				{
					/// Sum of skip run and coded mb bits accumulated.
					R += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
					mbSkipRun = 0;
				}//end if !_skip...
				else
					mbSkipRun++;

				/// An accurate early exit strategy is not possible because the model prediction require two valid (Dmax,R) points 
				/// for the whole frame. 

			}//end for mb...
	    /// Add last skip run.
			if (mbSkipRun)
				R += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

			/// Test the stopping criteria.
			int timeExceeded = 0;
			if (lclTimeLimit)
			{
				int timeSoFar = (int)(_codec->GetCounter()) - start;
				int avgTimePerIteration = timeSoFar / (1 + iterations);
				int timeLimit = lclTimeLimit - timeOffset - avgTimePerIteration;
				if (timeSoFar > timeLimit)
					timeExceeded = 1;
			}//end if lclTimeLimit...

			int rBndDiff = abs(Ru - Rl);			///< Optimal solution is rate bounded in a small enough range.
			int dDiff = abs(prevDmax - Dmax);	///< Has not changed by much from the previous iteration (converged).
			int rDiff = abs(bitTarget - R);		///< Close enough to the allowed bits.
			if ((rBndDiff < (4 * closeEnough)) ||
				((rDiff < closeEnough) || (/*(R <= allowedBits)&&*/(dDiff < closeEnoughDist))) ||
				(iterations > lclIterations) || timeExceeded)
			{
				/// The new point is the preferable point to choose but if the rate is not below allowedBits
				/// then the (Rl,Dl) point is selected. Use the invalidated signal to indicate if the mbs 
		    /// must be re-processed.
				if (R > allowedBits)
				{
					/// Load the lower vector as it is always below the target rate.
					memcpy((void *)_pQ, (const void *)_pQl, len * sizeof(int));
					R = Rl;
					invalidated = 1;
				}//end if R...
				else
					invalidated = 0;

				/// Optimal solution is found.
				done = 1;
			}//end if rDiff...
			else
			{
				if (allowedBits > R)	///< Inner predicted point is now lower point.
				{
					Dl = Dmax;
					Rl = R;
					/// pQ[] is the lower vector. 
					memcpy((void *)_pQl, (const void *)_pQ, len * sizeof(int));
					invalidated = 0;
				}//end if allowedBits...
				else							///< Inner predicted point is now upper point.
				{
					Du = Dmax;
					Ru = R;
					/// pQ[] must be reset to lower vector to maintain a descent convergence.
					memcpy((void *)_pQ, (const void *)_pQl, len * sizeof(int));
					invalidated = 1;	///< pQ[] now does not represented the state of the last encoded frame.
				}//end else...

			}//end else...

			iterations++;
		}//end while !done...

		/// Set the macroblock QP values to their found states.
		if (invalidated)
		{
			for (mb = 0; mb < len; mb++)
			{
				_codec->_pMb[mb]._mbQP = _pQ[mb];
				_codec->_pMb[mb]._mbEncQP = _pQ[mb];
			}//end for mb...
		}//end if invalidated...

		/// All good.
		//sprintf(_codec->_errorInfo, "P-frame: all good Allowed=%d Cost=%d", allowedBits, R);

	}//end if Rl...
	else	///< Damage control.
	{
		bitCost = DamageControl(allowedBits, Rl);

		if (bitCost >= allowedBits) ///< All failed.
			minMBIndex = 0;

		/// Damage control path.
		//sprintf(_codec->_errorInfo, "P-frame: damage control Allowed=%d Cost=%d", allowedBits, bitCost);

	}//end else Damage control...

	/*
	-------------------------------------------------------------------------------------------
	Residual macroblock final encoding.
	-------------------------------------------------------------------------------------------
	*/
	/// Rip through each macroblock as a linear array and process each block within the macroblock.
	/// The _mbEncPQ values have been set in the above encoding.
	mbSkipRun = 0;
	bitCount = 0;
	for (int mb = 0; mb < len; mb++)
	{
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);

		/// Encode the mbs and add to Ref img until min null mb position.
		if (mb < minMBIndex)
		{
			pMb->_mbQP = pMb->_mbEncQP; ///< Restore the QP that the mb was coded with.
	  //      bitCount += _codec->ProcessInterMbImplStd(pMb, 1, 0);
			bitCount += _codec->ProcessInterMbImplStd(pMb, 1, 1);
		}//end if mb...
		else
			bitCount += _codec->ProcessInterMbImplStdMin(pMb);
		if (!pMb->_skip)
		{
			bitCount += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
			mbSkipRun = 0;
		}//end if !_skip...
		else
			mbSkipRun++;

	}//end for mb...
  /// Add last skip run.
	if (mbSkipRun)
		bitCount += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

	*bitsUsed = 0;
	int ret = 1;
	if (bitCount > allowedBits)  ///< Failed to meet bit target.
	{
		_codec->_errorStr = "[H264Codec::InterImgPlaneEncoderImplMinMax::Encode] Bit target unattainable";
		ret = 0;
	}//end if bitCount...

	return(ret);
}//end InterImgPlaneEncoderImplMinMax::Encode.

/** Damage control algorithm for motion vectors only condition.
Clean up all remaining macroblocks as skipped and exit. In this implementation
when there are not enough bits to encode all the motion vectors the estimated list
is truncated with [0,0] difference vectors or [0,0] motion with special conditions
and re-compensated.
@param allowedBits  : Bits to use.
@param bitsUsed     : Bits consumed on the stream.
@return             : 1 = success, 0 = failed.
*/
int H264v2Codec::InterImgPlaneEncoderImplMinMax::DamageControlMvOnly(int allowedBits, int* bitsUsed)
{
	int mb;
	/// Stage 1: Start by taking a snapshot of improvement (mostly negative) in distortion between
	/// the current estimated mv and the predicted mv per mb. Count the total bit rate to determine
	/// how far off from the allwedBits these mvs are.
	int len = _codec->_mbLength;
	int bitCost = 0;
	int mbSkipRun = 0;
	for (mb = 0; mb < len; mb++)
	{
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.
		pMb->_include = 1;  ///< Default.

		/// Set the mb with the the mv, do the prediction to get the MVD and apply the motion compensation.
		pMb->_mbPartPredMode = MacroBlockH264::Inter_16x16; ///< Currently only 16x16 mode supported.
		/// Extract the 16x16 vector from the motion estimation result list.
		int mvx = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 0);
		int mvy = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 1);
		int topLeftX = pMb->_offLumX;
		int topLeftY = pMb->_offLumY;
		/// Get the predicted motion vector for this mb as the median of the neighbourhood vectors.
		int predX, predY;
		MacroBlockH264::GetMbMotionMedianPred(pMb, &predX, &predY);

		/// Compensate with the pred mv and measure the mb distortion.
		_codec->_pMotionCompensator->Invalidate();
		_codec->_pMotionCompensator->Compensate(topLeftX, topLeftY, predX, predY);
		int distortion = pMb->Distortion(_codec->_RefLum, _codec->_RefCb, _codec->_RefCr, _codec->_Lum, _codec->_Cb, _codec->_Cr);

		if ((mvx != predX) || (mvy != predY))  ///< Only if the estimated mv and the pred mv are not already equal.
		{
			_codec->_pMotionCompensator->Invalidate();
			_codec->_pMotionCompensator->Compensate(topLeftX, topLeftY, mvx, mvy);
			pMb->_distortion[0] = pMb->Distortion(_codec->_RefLum, _codec->_RefCb, _codec->_RefCr, _codec->_Lum, _codec->_Cb, _codec->_Cr);
		}//end if mvx...
		else
		{
			/// Estimated mv is equal to pred mv.
			pMb->_distortion[0] = distortion;
			pMb->_include = 0;
		}//end else...

		/// Expected increase in distortion by choosing the pred mv.
		_pDistortionDiff[mb] = distortion - pMb->_distortion[0];

		/// Store the motion vector in the mb.
		pMb->_mvX[MacroBlockH264::_16x16] = mvx;
		pMb->_mvY[MacroBlockH264::_16x16] = mvy;
		/// Set the mv difference before processing the mb.
		pMb->_mvdX[MacroBlockH264::_16x16] = mvx - predX;
		pMb->_mvdY[MacroBlockH264::_16x16] = mvy - predY;
		/// Process the compensated mb as motion vector only with zero'ed residual to measure its rate cost.
		int lclBitCost = _codec->ProcessInterMbImplStdMin(pMb);
		/// For non-skipped mbs, the previous skipped bit count must be included in the bit count.
		if (!pMb->_skip)
		{
			/// Sum the run and coding bit contributions.
			bitCost += (lclBitCost + _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun));
			mbSkipRun = 0; ///< Reset.
		}//end if !_skip...
		else  ///< Skipped.
			mbSkipRun++;
	}//end for mb...
  /// Add last skip run.
	if (mbSkipRun)
		bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

	/// Stage 2: Sub optimal solution proceeds by scanning the next mb with the initial least distortion 
	/// impact after replacing the estimated mv with the pred mv. Continue to replace the mv with the 
	/// pred mv until the accumulated bits are approximately below the allowed bits ignoring the skip run
	/// bit run contribution. Rely on the initial _rate[0] calculation above to guess the rate savings. 
	/// Then re-calculate actual bits at the end to determine whether or not the loop must continue.
	int forceStop = 0;
	while ((bitCost > allowedBits) && !forceStop)
	{
		///--------------------------------------------------------------------------------------
		int bitSavingRequired = bitCost - allowedBits;
		int savedBits = 0;
		while ((savedBits < bitSavingRequired) && !forceStop)
		{
			int currentLowestDistDiff = 0x7FFFFFFF; ///< Very large starting point.
			/// Find the next lowest distortion difference.
			int nextMb = -1;
			for (mb = 0; mb < len; mb++)
			{
				MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.
				if (pMb->_include && (_pDistortionDiff[mb] < currentLowestDistDiff))
				{
					currentLowestDistDiff = _pDistortionDiff[mb];
					nextMb = mb;
				}//end if _include...
			}//end for mb...

			if (nextMb >= 0) ///< A candidate mb was found.
			{
				/// Approximate the saving by the existing rate[0] value by assuming that
				/// this mb will be skipped. Ignore the savings on the improved skip run.
				savedBits += _codec->_pMb[nextMb]._rate[0];
				/// Exclude from further checks.
				_codec->_pMb[nextMb]._include = 0;
			}//end if nextMb...
			else
				forceStop = 1;

		}//end while savedBits...

		///--------------------------------------------------------------------------------------
		/// Re-process each mb and accumulate the bit cost. All mbs that are not included must
		/// be set to the pred mv. Only those mbs where the mv has changed are re-compensated.
		bitCost = 0;
		mbSkipRun = 0;
		for (mb = 0; mb < len; mb++)
		{
			MacroBlockH264* pMb = &(_codec->_pMb[mb]);

			/// The pred mv may now be different.
			int predX, predY;
			MacroBlockH264::GetMbMotionMedianPred(pMb, &predX, &predY);

			if (!pMb->_include)  ///< Marked as a previously predicted mv and mv was set to pred mv.
			{
				/// Set the result mv list to the pred mv.
				_codec->_pMotionEstimationResult->SetSimpleElement(mb, 0, predX);
				_codec->_pMotionEstimationResult->SetSimpleElement(mb, 1, predY);

				/// Check that this mv is the pred mv.
				if ((pMb->_mvX[MacroBlockH264::_16x16] != predX) || (pMb->_mvY[MacroBlockH264::_16x16] != predY))
				{
					/// Re-compensate if it has changed.
					_codec->_pMotionCompensator->Invalidate();
					_codec->_pMotionCompensator->Compensate(pMb->_offLumX, pMb->_offLumY, predX, predY);
				}//end if _mvX...

			}//end if !include...

			/// Store the motion vector and pred mv in the mb.
			int mvx = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 0);
			int mvy = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 1);
			pMb->_mvX[MacroBlockH264::_16x16] = mvx;
			pMb->_mvY[MacroBlockH264::_16x16] = mvy;
			pMb->_mvdX[MacroBlockH264::_16x16] = mvx - predX;
			pMb->_mvdY[MacroBlockH264::_16x16] = mvy - predY;
			int r = _codec->ProcessInterMbImplStdMin(pMb);
			/// For non-skipped mbs, the previous skipped bit count must be included in the bit count.
			if (!pMb->_skip)
			{
				/// Sum the run and coding bit contributions.
				bitCost += (r + _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun));
				mbSkipRun = 0; ///< Restart
			}//end if !_skip...
			else  ///< Skipped.
				mbSkipRun++;

		}//end for mb...

		/// Add last skip run.
		if (mbSkipRun)
			bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

	}//end while bitCost...

	/// Check that a solution is possible.
	if (forceStop)
	{
		*bitsUsed = 0;
		_codec->_errorStr = "[H264Codec::InterImgPlaneEncoderImplMinMax::Encode] Forced stop - no solution possible";
		return(0); ///< Failed.
	}//end if allowedBits...

	  /// ...and then exit.
	int ret = 1;
	if (bitCost > allowedBits)
	{
		_codec->_errorStr = "[H264Codec::InterImgPlaneEncoderImplMinMax::Encode] Bits exceeded in trucation";
		ret = 0;
	}//end if bitCost...

	*bitsUsed = 0;
	return(ret); ///< Something is coded so it is considered successful.
}//end InterImgPlaneEncoderImplMinMax::DamageControlMvOnly.

/** Damage control algorithm
Their are insufficient bits at the max QP and alternative quantisation approaches are required.
@param  allowedBits : Bits to use.
@param  currBitCost : Bits used so far for the last encoding of mv and max QP
@return             : Bit cost of this implementation.
*/
int H264v2Codec::InterImgPlaneEncoderImplMinMax::DamageControl(int allowedBits, int currBitCost)
{
	///======================================================================================

	/// Sub-optimal Steepest Ascent Algorithm

	int i, mb;
	int len = _codec->_mbLength;
	int bitCost = currBitCost;
	int iterations = 0;

	/// Initialise the ordered mb list where skipped mbs will never become unskipped and can therefore be excluded.
	int listLen = 0;
	for (mb = 0; mb < len; mb++)
	{
		if (!_codec->_pMb[mb]._skip)
			_pMbList[listLen++] = mb;
	}//end for mb...

	while ((bitCost >= allowedBits) && (listLen > 1))
	{
		int predR = bitCost;

		while ((listLen > 1) && (predR >= allowedBits))
		{
			/// Step 1: Order the non-skipped mbs in increasing distortion.

			/// Only included newly skipped and completed mbs from the old list.
			int currListLen = listLen;
			listLen = 0;
			for (i = 0; i < currListLen; i++)
			{
				mb = _pMbList[i];
				if (!_codec->_pMb[mb]._skip && (_codec->_pMb[mb]._mbEncQP < H264V2_MAX_EXT_QP))
					_pMbList[listLen++] = mb;
			}//end for mb...

			/// Bubble sort list in ascending order of distortion.
			int cnt = len;
			while (cnt)
			{
				cnt = 0; ///< Count how many re-orderings occur.
				for (i = 1; i < listLen; i++)
				{
					MacroBlockH264* pMb1 = &(_codec->_pMb[_pMbList[i - 1]]);
					MacroBlockH264* pMb2 = &(_codec->_pMb[_pMbList[i]]);
					if (pMb2->_distortion[pMb2->_mbEncQP] < pMb1->_distortion[pMb1->_mbEncQP])
					{
						/// Swap places.
						int tmp = _pMbList[i - 1];
						_pMbList[i - 1] = _pMbList[i];
						_pMbList[i] = tmp;
						cnt++;
					}//end if _distortion...
				}//end for i...
			}//end while cnt...

			/// Step 2: Increase the distortion of mb at the head of the ordered list to just more than the
			/// next one in the list.
			int mb1 = _pMbList[0]; /// Head of ordered list.
			int mb2 = _pMbList[1]; /// Next in ordered list.
			int d1 = _codec->_pMb[mb1]._distortion[_codec->_pMb[mb1]._mbEncQP];
			int d2 = _codec->_pMb[mb2]._distortion[_codec->_pMb[mb2]._mbEncQP];

			/// For the unusual case where the listLen == 1, set them to the same.
			if (listLen == 1)
				d2 = d1;

			if ((d1 <= d2) && (_codec->_pMb[mb1]._mbEncQP < H264V2_MAX_EXT_QP))
			{
				MacroBlockH264* pMb = &(_codec->_pMb[mb1]);  ///< Simplify mb addressing.

				predR -= pMb->_rate[pMb->_mbEncQP]; ///< Remove old bits before processing.
				while ((pMb->_distortion[pMb->_mbEncQP] <= d2) && (pMb->_mbEncQP < H264V2_MAX_EXT_QP))
				{
					/// Select new QP.
					switch (pMb->_mbEncQP)
					{
					case 51: pMb->_mbQP = 55; break;
					case 55: pMb->_mbQP = 59; break;
					case 59: pMb->_mbQP = 63; break;
					case 63: pMb->_mbQP = 67; break;
					case 67: pMb->_mbQP = 68; break;
					case 68: pMb->_mbQP = 69; break;
					case 69: pMb->_mbQP = 70; break;
					case 70: pMb->_mbQP = 71; break;
					default: pMb->_mbQP = 71; break;
					}///end switch _mbEncQP...

					_codec->ProcessInterMbImplStd(pMb, 0, 2);

				}//end while _distortion[]...

				/// Add back new bits.
				if (!pMb->_skip)
				{
					pMb->_rate[pMb->_mbEncQP] = _codec->MacroBlockLayerBitCounter(pMb);
					predR += pMb->_rate[pMb->_mbEncQP];
				}//end if !_skip ...

			}//end if d1...

		}//end while listLen...

		/// Step 3: Test the new solution.
		int mbSkipRun = 0;
		bitCost = 0;
		for (int mb = 0; mb < len; mb++)
		{
			MacroBlockH264* pMb = &(_codec->_pMb[mb]);

			pMb->_mbQP = pMb->_mbEncQP; ///< Restore the QP that the mb was coded with.
			bitCost += _codec->ProcessInterMbImplStd(pMb, 0, 1);
			if (!pMb->_skip)
			{
				bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
				mbSkipRun = 0;
			}//end if !_skip...
			else
				mbSkipRun++;

		}//end for mb...
	  /// Add last skip run.
		if (mbSkipRun)
			bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

		iterations++;
	}//end while bitCost...

	return(bitCost);

	//    if(bitCost >= allowedBits) ///< All failed.
	//		  minMBIndex = 0;

		///======================================================================================
		/*
		/// Sub-optimal Steepest Descent Algorithm

		/// QP = H264V2_MAX_QP for all mbs still uses too many bits so the extended range of QP = {51..71} is
		/// used here to find a near optimal solution.
		int i;
		bitCost = 0;

		/// Step 1: Test that QP = {71} will fit and set it to the initialisation of the optimisation algorithm.
		/// Initialise the ordered mb list.
		int listLen = 0;
		mbSkipRun = 0;
		  for(mb = 0; mb < len; mb++)
		  {
		  MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.

		  _pMbList[listLen++] = mb; ///< Arbitrary order for the mb list.

		  /// Note that the 1st mb will have a delta offset from _slice._qp to get it to _mbQP = H264V2_MAX_QP.
			  pMb->_mbQP = H264V2_MAX_EXT_QP;

			  /// Accumulate the rate and find the largest distortion for all mbs. Call returns 0 for skipped mbs. The ref
		  /// holding the compensated motion is not updated with the encoding in this traversal of the mbs.
			  bitCost += _codec->ProcessInterMbImplStd(pMb, 0, 1);
		  if(!pMb->_skip)
		  {
			/// Sum of skip run and coded mb bits accumulated.
			bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
			mbSkipRun = 0;
		  }//end if !_skip...
		  else
			mbSkipRun++;

			  /// Early exit if this min point is still not possible.
			  if(bitCost >= allowedBits)
				  break;

		  }//end for mb...
		/// Add last skip run.
		if(mbSkipRun)
		  bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

		R = 0;
		iterations  = 0;

			/// Only proceed if there is hope.
			if(bitCost < allowedBits)
		{
		  /// Starting from highest distortion and lowest rate, slowly decrease the distortion
		  /// and count the approx bit improvement until a good guess is acheived. Then test the
		  /// guess for the actual improvement. Reverse up the audit trail until it fits.

		  /// Audit trail initialisation. -1 indicates no entry.
		  memset(_pLastMbCoded, 0xFF, len * sizeof(int));
		  int lastMbPos = 0;

		  /// Repeat until the solution converges but limit it to 3 iterations for speed.
		  int prevBitCost = 0;
		  while( (bitCost > prevBitCost) && (iterations < 3) )
		  {
			prevBitCost = bitCost;
			int predR = bitCost;
			/// Make a guess by iteratively adjusting predicted rate predR
			while( (listLen > 1)&&(predR < allowedBits) )
			{
			  /// Step 2: Order the mbs in decreasing distortion.

			  /// Included all mbs from the old list except for those that have decreased down
			  /// to QP = {51}.
			  int currListLen = listLen;
			  listLen = 0;
			  for(i = 0; i < currListLen; i++)
			  {
				mb = _pMbList[i];
				if(_codec->_pMb[mb]._mbEncQP > H264V2_MAX_QP)
				  _pMbList[listLen++] = mb;
			  }//end for mb...

			  /// Bubble sort list in descending order of distortion.
			  int cnt = len;
			  while(cnt)
			  {
				cnt = 0; ///< Count how many re-orderings occur.
				for(i = 1; i < listLen; i++)
				{
				  MacroBlockH264* pMb1 = &(_codec->_pMb[_pMbList[i-1]]);
				  MacroBlockH264* pMb2 = &(_codec->_pMb[_pMbList[i]]);
				  if( pMb2->_distortion[pMb2->_mbEncQP] > pMb1->_distortion[pMb1->_mbEncQP] )
				  {
					/// Swap places.
					int tmp       = _pMbList[i-1];
					_pMbList[i-1] = _pMbList[i];
					_pMbList[i]   = tmp;
					cnt++;
				  }//end if _distortion...
				}//end for i...
			  }//end while cnt...

			  /// Step 3: Decrease the distortion of mb at the head of the ordered list to just less than the
			  /// next one in the list.
			  int mb1 = _pMbList[0]; /// Head of ordered list.
			  int mb2 = _pMbList[1]; /// Next in ordered list.
			  int d1 = _codec->_pMb[mb1]._distortion[_codec->_pMb[mb1]._mbEncQP];
			  int d2 = _codec->_pMb[mb2]._distortion[_codec->_pMb[mb2]._mbEncQP];

			  /// For the unusual case where the listLen == 1, set them to the same.
			  if(listLen == 1)
				d2 = d1;

			  MacroBlockH264* pMb = &(_codec->_pMb[mb1]);  ///< Simplify mb addressing.

			  /// Store the current mb encoding in the audit trail lists.
			  _pLastMbCoded[lastMbPos]  = mb1;
			  _pLastMbQP[lastMbPos++]   = pMb->_mbEncQP;
			  if(lastMbPos >= len) ///< Loop around.
				lastMbPos = 0;

			  R = pMb->_rate[pMb->_mbEncQP];  ///< Remember R at this point in case the loop below does not execute.
			  predR -= pMb->_rate[pMb->_mbEncQP]; ///< Remove old bits before processing.
			  while( (pMb->_distortion[pMb->_mbEncQP] >= d2) && (pMb->_mbEncQP > H264V2_MAX_QP) && ((predR+R) <= allowedBits) )
			  {
				/// Select new QP and process.
				pMb->_mbQP = NextQPDec[pMb->_mbEncQP];
				  R = _codec->ProcessInterMbImplStd(pMb, 0, 1);
			  }//end while _distortion[]...

			  /// Add back new bits.
			  predR += R;

			}//end while listLen...

			/// Step 3: Test the new solution. On exit of the above loop, the approx
			/// bit rate is just too high and therefore the audit trail is used to reverse
			/// until it fits.

			if(lastMbPos != 0)  ///< Back up by 1.
			  lastMbPos--;
			else ///< Wrap around.
			  lastMbPos = len - 1;

			bitCost = predR;  ///< Start with the guess made above.

			while( (bitCost >= allowedBits) && (_pLastMbCoded[lastMbPos] != -1) )
			{
			  _codec->_pMb[_pLastMbCoded[lastMbPos]]._mbEncQP = _pLastMbQP[lastMbPos];
			  _pLastMbCoded[lastMbPos] = -1;  ///< Mark as used goods.
			  if(lastMbPos != 0)  ///< Back up by 1.
				lastMbPos--;
			  else ///< Wrap around.
				lastMbPos = len - 1;

			  mbSkipRun = 0;
				bitCost = 0;
				for(int mb = 0; mb < len; mb++)
				{
				MacroBlockH264* pMb = &(_codec->_pMb[mb]);

				pMb->_mbQP = pMb->_mbEncQP; ///< Restore the QP that the mb was coded with.
				bitCost += _codec->ProcessInterMbImplStd(pMb, 0, 1);
				if(!pMb->_skip)
				{
				  bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
				  mbSkipRun = 0;
				}//end if !_skip...
				else
				  mbSkipRun++;

				}//end for mb...
			  /// Add last skip run.
			  if(mbSkipRun)
				bitCost += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
			}//end while bitCost...

			iterations++;
		  }//end while bitCost...

		}//end if bitCost...
		else ///< All failed so mark all for min encodings and hope for the best.
		{
			  minMBIndex = 0;
		}//end else...
		*/
		///======================================================================================
		 /*
		 /// Alternating Bi-Section Algorithm.

		 Ru = Rl;  ///< Upper is now QP = {51} determined above.
		 Du = Dl;
		 Rl = 0;
		 Dl = 0;
		 mbDmax = 0;
		 qEnd   = H264V2_MAX_QP;

		 /// Step 1: Test that QP = {71} will fit and set it to the initialisation of the optimisation algorithm.
		 mbSkipRun = 0;
		   for(mb = 0; mb < len; mb++)
		   {
		   MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.

		   /// Note that the 1st mb will have a delta offset from _slice._qp to get it to _mbQP = H264V2_MAX_QP.
			   pMb->_mbQP	  = H264V2_MAX_EXT_QP;
			   /// Include all macroblocks at the start.
			   pMb->_include = 1;
			   _pQl[mb]      = H264V2_MAX_EXT_QP;
			   _pQ[mb]	      = H264V2_MAX_EXT_QP;	///< Active quantiser vector is always the lower (max distortion) side.

			   /// Accumulate the rate and find the largest distortion for all mbs. Call returns 0 for skipped mbs. The ref
		   /// holding the compensated motion is not updated with the encoding in this traversal of the mbs.
			   Rl += _codec->ProcessInterMbImplStd(pMb, 0, 1);
		   if(!pMb->_skip)
		   {
			 /// Sum of skip run and coded mb bits accumulated.
			 Rl += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
			 mbSkipRun = 0;
		   }//end if !_skip...
		   else
			 mbSkipRun++;

			   if(pMb->_distortion[H264V2_MAX_EXT_QP] > Dl)
			   {
				   Dl = pMb->_distortion[H264V2_MAX_EXT_QP];
				   mbDmax = mb;	///< Mark the mb index with the peak distortion.
			   }//end if _distortion...

			   /// Early exit if this min point is still not possible. Mark all as min encodings and hope it fits.
			   if(Rl > allowedBits)
		   {
				   minMBIndex = 0;
				   break;
		   }//end if Rl...

		   }//end for mb...
		 /// Add last skip run.
		 if(mbSkipRun)
		   Rl += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

		 /// Step 2: Proceed with optimisation algorithm with QP = {51..71}
		 if(Rl < allowedBits)
		 {
			   /// Create an epsilon at 0.4% of the available bits with a min limit of 16 bits.
			   /// Adjust the available bits to be epsilon below allowedBits.
			   int closeEnough = allowedBits/250;	///< 0.4%
			   if(closeEnough < 16)
				   closeEnough = 16;
			   int closeEnoughDist = 8;	///< Sqr error per macroblock.

		   /// Model based faster algorithm to improve on the bi-section method.

			   /// Modify the target rate to a close offset on the low rate side.
			   int bitTarget = allowedBits - closeEnough;

			   /// Initialisation is complete and the adapting bi-section/power model algorithm starts
			   /// here. The stopping criteria are 1) a rate less than, but close to, the allowed bits
			   /// or 2) small change in distortion when reducing the interval.
			   int done = 0;
			   while(!done)
			   {
				   int prevDmax = Dmax;

				   /// Make a prediction assuming a power law model.
	   //			Dmax = _codec->FitDistPowerModel(Rl, Dl, Ru, Du, bitTarget);
				   /// Only use this prediction if it is bounded by (Rl,Dl) and (Ru,Du).
	   //			if( (Dmax < Du)||(Dmax > Dl) )	///< Out of bound.
	   //				Dmax = _codec->FitDistLinearModel(Rl, Dl, Ru, Du, bitTarget);

				   /// Encourage the descent direction by adding a decreasing factor to the
				   /// predicted Dmax such that convergence to Dmax is from the (Rl,Dl) side.
	   //		  Dmax += abs(Dl - Dmax)/4;

	   //			if( (Dmax < Du)||(Dmax > Dl)||(Dmax == prevDmax) )	///< Still out of bound.
					   Dmax = ((Du + Dl) + 1) >> 1;	///< Set the midpoint max distortion.

				   /// At each macroblock reduce the quant value until the distortion is lower
				   /// than Dmax. pQ[] must always hold the lower rate (smaller valued) quant vector
				   /// as the previous best choice.
				   R = 0;
				   int firstMbChange = len;
				   if(invalidated)	///< Frame encoded coeffs must be invalidated if _pQ[] does not represent the actual QPs used.
					   firstMbChange = 0;
			 mbSkipRun = 0;
				   for(mb = 0; mb < len; mb++)
				   {
					   MacroBlockH264* pMb = &(_codec->_pMb[mb]);

					   /// Record the found QP where the macroblock dist is just below Dmax and accumulate the rate for this macroblock.
					   _pQ[mb] = _codec->GetMbQPBelowDmaxVer3(*pMb, _pQ[mb], Dmax, &firstMbChange, qEnd, FALSE);
					   R += pMb->_rate[_pQ[mb]]; ///< Rate = 0 for skipped mbs.
			   if(!pMb->_skip)
			   {
				 /// Sum of skip run and coded mb bits accumulated.
				 R += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
				 mbSkipRun = 0;
			   }//end if !_skip...
			   else
				 mbSkipRun++;

					   /// An accurate early exit strategy is not possible because the model prediction require two valid (Dmax,R) points
					   /// for the whole frame.

				   }//end for mb...
			 /// Add last skip run.
			 if(mbSkipRun)
			   R += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);

				   /// Test the stopping criteria.
				   int rBndDiff = abs(Ru - Rl);			///< Optimal solution is rate bounded in a small enough range.
				   int dDiff = abs(prevDmax - Dmax);	///< Has not changed by much from the previous iteration (converged).
				   int rDiff = abs(bitTarget - R);		///< Close enough to the allowed bits.
				   if( (rBndDiff < (4*closeEnough)) ||
						   ( (rDiff < closeEnough)||((dDiff < closeEnoughDist))) ||
				 (iterations > _codec->_interIterations) )
				 {
					   /// The new point is the preferable point to choose but if the rate is not below allowedBits
					   /// then the (Rl,Dl) point is selected. Use the invalidated signal to indicate if the mbs
			   /// must be re-processed.
					   if(R > allowedBits)
					   {
						   /// Load the lower vector as it is always below the target rate.
						   memcpy((void *)_pQ, (const void *)_pQl, len * sizeof(int));
						   R = Rl;
				 invalidated = 1;
					   }//end if R...
			   else
				 invalidated = 0;

					   /// Optimal solution is found.
					   done = 1;
				   }//end if rDiff...
				   else
				   {
					   if(allowedBits > R)	///< Inner predicted point is now lower point.
					   {
						   Dl	= Dmax;
						   Rl	= R;
						   /// pQ[] is the lower vector.
						   memcpy((void *)_pQl, (const void *)_pQ, len * sizeof(int));
						   invalidated = 0;
					   }//end if allowedBits...
					   else							///< Inner predicted point is now upper point.
					   {
						   Du	= Dmax;
						   Ru	= R;
						   /// pQ[] must be reset to lower vector to maintain a descent convergence.
						   memcpy((void *)_pQ, (const void *)_pQl, len * sizeof(int));
						   invalidated = 1;	///< pQ[] now does not represented the state of the last encoded frame.
					   }//end else...

				   }//end else...

				   iterations++;
			   }//end while !done...

			   /// Set the macroblock QP values to their found states.
		   if(invalidated)
		   {
				 for(mb = 0; mb < len; mb++)
				 {
					 _codec->_pMb[mb]._mbQP    = _pQ[mb];
					 _codec->_pMb[mb]._mbEncQP = _pQ[mb];
				 }//end for mb...
		   }//end if invalidated...

		 }//end if Rl...
		 */
		 ///======================================================================================
		 /*
		 /// Simple Truncation Algorithm

			 /// If QP = H264V2_MAX_QP and still does not fit then a truncation process is nessecary. The truncation
			 /// is somewhere between every mb encoded with motion with zero'ed coeffs and some mbs with max QP. The
		 /// _rate[0] member of the mbs holds the min encoding and is zero for skipped. Proceed by encoding each
		 /// skip run and max QP pair per mb and then count the bits to the end.
			 R         = 0;	///< Reset.
		 mbSkipRun = 0;
			 for(mb = 0; mb < len; mb++)
			 {
		   MacroBlockH264* pMb = &(_codec->_pMb[mb]);  ///< Simplify mb addressing.

		   /// Do mb encoding including motion and max QP.
			   pMb->_mbQP = H264V2_MAX_QP;
			   R += _codec->ProcessInterMbImplStd(pMb, 0, 1);  ///< Returns 0 if skipped.
		   if(!pMb->_skip)
		   {
			 /// Sum of skip run and coded mb bits accumulated.
			 R += _codec->_pHeaderUnsignedVlcEnc->Encode(mbSkipRun);
			 mbSkipRun = 0;

			 /// Add up min encodings from the next mb to the end.
			 int bitsToEnd = 0;
			 int run       = 0;
			 int mbi       = mb + 1;
			 while(mbi < len)
			 {
			   MacroBlockH264* pMbi = &(_codec->_pMb[mbi]);
			   int r = pMbi->_rate[0];
			   if(r) ///< Not skipped.
			   {
				 bitsToEnd += (r + _codec->_pHeaderUnsignedVlcEnc->Encode(run));
				 run = 0;
			   }//end if r...
			   else
				 run++;
			   mbi++;
			 }//end while mbi...
			 if(run)
				bitsToEnd += _codec->_pHeaderUnsignedVlcEnc->Encode(run);

			 /// With this mb max QP encoded and min encoding for all remaining mbs to the end,
			 /// if it does not fit then mark it as the min encoding point and break.
			 if((R + bitsToEnd) >= allowedBits)
			 {
					   minMBIndex = mb;
					   break;
			 }//end if R...
		   }//end if !_skip...
		   else
			 mbSkipRun++;

			 }//end for mb...
		 */
}//end DamageControl.

/** Encode the image data for Inter pictures with a Dmax criterion.
Process all macroblocks in order with a mb Dmax limit and only write the result
to the ref image space if the writeRef code is set. Bit 1 of writeRef refers to
motion compensation and bit 0 to adding the difference to the ref. The motion
estimation process with its associated data structures is assumed to have been
completed before this method is called. The macroblock obj is prepared for
coding onto the bit stream.
Note: For iteratively calling this method;
	Call 1:			writeRef = 2 (compensation to ref but no adding).
	Call 2 - X: writeRef = 0 (ref is motion compensated already from call 1).
	Call final:	writeRef = 1 (allow final add to ref).
@param allowedBits	: Total remaining bits to target.
@param bitsUsed			: Num of bits used for this encoding.
@param writeRef			: Bit code 1x/0x = do/do not comp. x1/x0 do/do not add ref.
@return							: 1 = success, 0 = error.
*/
int H264v2Codec::InterImgPlaneEncoderImplDMax::Encode(int allowedBits, int* bitsUsed, int writeRef)
{
	/// Whip through each macroblock and encode. The stream writing of
	/// the macroblock is seperate to allow further rate-distortion decision 
	/// making later on in the encoding process.
	int compRef = writeRef & 2;
	int addRef = writeRef & 1;
	int len = _codec->_mbLength;
	int dmax = _codec->_dMax;
	/// Set a bound on the max quality by choosing a min qp value that the MinMax search is limited to.
	int qEnd = _codec->_minQPInter;
	int coeffBits = 0;

	/// Slice without partitioning and therefore only one set of slice parameters.

	/// Set up the input and ref image mem overlays.
	_codec->_Lum->SetOverlayDim(4, 4);
	_codec->_Cb->SetOverlayDim(4, 4);
	_codec->_Cr->SetOverlayDim(4, 4);
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOverlayDim(8, 8);

	/// All integer transforms are Inter in this method.
	_codec->_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TLum->SetParameter(IForwardTransform::INTRA_FLAG_ID, 0);
	_codec->_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
	_codec->_pF4x4TChr->SetParameter(IForwardTransform::INTRA_FLAG_ID, 0);
	/// By default the DC transforms were set in the TransformOnly mode in the Open() method.

	/// Motion estimation has been previously performed outside of this method and therefore
	/// only motion compensation is required here. Prepare for motion compensation on a per 
	/// macoblock basis. The vectors themselves are held in _pMotionEstimationResult.
	if (compRef)
		_codec->_pMotionCompensator->PrepareForSingleVectorMode();

	/// Get the motion vector list to work with. Assume SIMPLE2D type list as only a
	/// single 16x16 motion vector is considered per macroblock in this implementation
	/// i.e. Inter_16x16 mode only
	int	listLen = _codec->_pMotionEstimationResult->GetLength();
	if (listLen != len)
	{
		*bitsUsed = 0;
		return(0);	///< Error: Motion vector list must match.
	}//end if listLen...

  ///---------------------- Macroblock Process ------------------------------------------
	/// Rip through each macroblock as a linear array and process the
	/// motion vector and each block within the macroblock.
  int accumulatedD = 0;
	for (int mb = 0; mb < len; mb++)
	{
		/// Simplify the referencing to the current macroblock.
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);

		///------------------- Motion compensation ------------------------------------------------
		pMb->_mbPartPredMode = MacroBlockH264::Inter_16x16;	///< Fixed at 16x16 for now.

		/// Get the 16x16 motion vector from the motion estimation result list and apply
		/// it to the macroblock. The reference image will then hold the compensated macroblock
		/// to be used as the prediction for calcualting the residual.
		int mvx = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 0);
		int mvy = _codec->_pMotionEstimationResult->GetSimpleElement(mb, 1);
		if (compRef)
		{
			_codec->_pMotionCompensator->Compensate(pMb->_offLumX, pMb->_offLumY, mvx, mvy);
		}//end if compRef...

			/// Store the vector for this macroblock.
		pMb->_mvX[MacroBlockH264::_16x16] = mvx;
		pMb->_mvY[MacroBlockH264::_16x16] = mvy;

		/// Get the predicted motion vector for this mb as the median of the neighbourhood 
		/// vectors and subtract it from the mb vector.
		int predX, predY;
		MacroBlockH264::GetMbMotionMedianPred(pMb, &predX, &predY);
		pMb->_mvdX[MacroBlockH264::_16x16] = mvx - predX;
		pMb->_mvdY[MacroBlockH264::_16x16] = mvy - predY;

		///------------------- Macroblock processing ----------------------------------------------
	  /// Note that the 1st mb will have a delta offset from _slice._qp to get it to _mbQP = H264V2_MAX_QP.
		pMb->_mbQP = H264V2_MAX_QP;

		/// For desperate measures when the previous encoding failed to reach the targeted rate then 
		/// a further quantisation process can be implemented here where the coeffs will be zeroed. This
		/// has been tested but is not implemented because the effectiveness is limited.
		if (_codec->_pRateCntlPFrames != NULL)
		{
			if (_codec->_pRateCntlPFrames->OutOfBounds() && _codec->_pRateCntlPFrames->LowerDistortionOverflow())
				pMb->_mbQP = H264V2_MAX_QP + 16;
		}//end if _pRateCntlPFrames...

		/// Include all macroblocks.
		pMb->_include = 1;

    /// Find QP where the mb Lum dist is just below Dmax for this mb. Count the bits for the coeffs only.
    if (_codec->_modeOfOperation == H264V2_MINAVG_RATECNT)
    {
      /// In this mode of operation the mb Dmax parameter is interpreted as Davg per mb.
      int davg = dmax;
      if (mb)  ///< All mbs except the 1st
      {
        /// Determine the avg per mb for the remaining mbs.
        davg = ((len*dmax) - accumulatedD) / (len - mb);
        if (davg <= 0) davg = dmax; ///< If all goes wrong then revert to dmax
      }//end if mb...

      coeffBits += _codec->ProcessInterMbImplStd(pMb, addRef, 3, davg, qEnd);
      accumulatedD += pMb->_distortion[pMb->_mbEncQP];
    }//end if H264V2_MINAVG_RATECNT...
    else if (_codec->_modeOfOperation == H264V2_MINMAX_RATECNT)
      coeffBits += _codec->ProcessInterMbImplStd(pMb, addRef, 3, dmax, qEnd);
    else
			_codec->ProcessInterMbImplStd(pMb, addRef, 2, dmax, qEnd);

	}//end for mb...

  /// Store the coeff bits per pel.
	_codec->_coeffBitsPerPel = (double)coeffBits / (double)(_codec->_lumWidth*_codec->_lumHeight);
  if (_codec->_modeOfOperation == H264V2_MINAVG_RATECNT)
    _codec->_frameDistortion = accumulatedD / len;  ///< Avg distortion per mb.
  else
    _codec->_frameDistortion = dmax;  ///< Max mb distortion for all mbs.

	*bitsUsed = 0;
	return(1);
}//end InterImgPlaneEncoderImplDMax::Encode.

/** Decode the Inter macroblocks to the reference img.
The macroblock obj encodings must be fully defined before calling
this method.
@return	: 1 = success, 0 = error.
*/
int H264v2Codec::InterImgPlaneDecoderImplStdVer1::Decode(void)
{
	int mb;
	int len = _codec->_mbLength;

	/// Set up the input and ref image mem overlays.
	_codec->_RefLum->SetOverlayDim(4, 4);
	_codec->_RefCb->SetOverlayDim(4, 4);
	_codec->_RefCr->SetOverlayDim(4, 4);
	_codec->_16x16->SetOverlayDim(16, 16);
	_codec->_8x8_0->SetOverlayDim(8, 8);
	_codec->_8x8_1->SetOverlayDim(8, 8);

	/// Prepare for motion compensation on a per macoblock basis.
	_codec->_pMotionCompensator->PrepareForSingleVectorMode();

	/// Whip through each macroblock. Decode the extracted encodings. All modes
	/// and parameters have been extracted by the ReadMacroBlockLayer() method.
	for (mb = 0; mb < len; mb++)
	{
		/// Simplify the referencing to the current macroblock.
		MacroBlockH264* pMb = &(_codec->_pMb[mb]);
		int lOffX = pMb->_offLumX;
		int lOffY = pMb->_offLumY;
		int cOffX = pMb->_offChrX;
		int cOffY = pMb->_offChrY;

		///------------------- Motion compensation -----------------------------------------------------------
		if (pMb->_mbPartPredMode != MacroBlockH264::Inter_16x16)	///< Fixed at 16x16 mode for now.
		{
			_codec->_errorStr = "[H264V2::InterImgPlaneDecoderImplStdVer1::Decode] Only supports Inter_16x16 mode";
			return(0);
		}//end if !Inter_16x16...

		/// Compensate the vector and hope the encoder estimator ensured that
		/// all pels fall inside the image space. The motion vector was decoded
		/// from the vector differences in the ReadMacroBlockLayer() method.
		_codec->_pMotionCompensator->Compensate(lOffX, lOffY, pMb->_mvX[MacroBlockH264::_16x16], pMb->_mvY[MacroBlockH264::_16x16]);

		if (pMb->_coded_blk_pattern)
		{
			/// --------------------- Inverse Transform & Quantisation -------------------------------
			if (pMb->_mbPartPredMode == MacroBlockH264::Inter_16x16)
				_codec->InverseTransAndQuantInter16x16MBlk(pMb, 0);

			/// --------------------- Image Storing into Ref -----------------------------------------
			/// Fill the image (difference) colour components from all the non-DC 4x4 
			/// blks (i.e. Not blks = -1, 17, 18) of the macroblock temp blocks. 
			MacroBlockH264::StoreBlks(pMb, _codec->_16x16, 0, 0, _codec->_8x8_0, _codec->_8x8_1, 0, 0, 0);

			/// --------------------- Add the prediction ---------------------------------------------
			/// Lum.
			_codec->_RefLum->SetOverlayDim(16, 16);
			_codec->_RefLum->SetOrigin(lOffX, lOffY); ///< Align the Ref Lum img block with this macroblock.
			_codec->_16x16->SetOverlayDim(16, 16);
			_codec->_16x16->SetOrigin(0, 0);
			_codec->_RefLum->AddWithClip255(*(_codec->_16x16));	///< Add to ref Lum and leave result in ref img.
			/// Cb.
			_codec->_RefCb->SetOverlayDim(8, 8);
			_codec->_RefCb->SetOrigin(cOffX, cOffY);
			_codec->_8x8_0->SetOverlayDim(8, 8);
			_codec->_8x8_0->SetOrigin(0, 0);
			_codec->_RefCb->AddWithClip255(*(_codec->_8x8_0));
			/// Cr.
			_codec->_RefCr->SetOverlayDim(8, 8);
			_codec->_RefCr->SetOrigin(cOffX, cOffY);
			_codec->_8x8_1->SetOverlayDim(8, 8);
			_codec->_8x8_1->SetOrigin(0, 0);
			_codec->_RefCr->AddWithClip255(*(_codec->_8x8_1));

		}//end if _coded_blk_pattern...

	}//end for mb...

	return(1);
}//end InterImgPlaneDecoderImplStdVer1::Decode.

/*
-------------------------------------------------------------------------------------------
	Private helper methods for prediction tools.
-------------------------------------------------------------------------------------------
*/

/** Calc the median of 3 numbers.
@param x	:	1st num.
@param y	:	2nd num.
@param z	:	3rd num.
@return		: The median of x, y and z.
*/
int H264v2Codec::Median(int x, int y, int z)
{
	int min, max;

	// min = MIN(x,MIN(y,z)) and max = MAX(x,MAX(y,z)).
	if ((y - z) < 0) { min = y;	max = z; }
	else { min = z; max = y; }
	if (x < min) min = x;
	if (x > max) max = x;
	// Median.
	int result = x + y + z - min - max;

	return(result);
}//end Median.

void H264v2Codec::DumpBlock(OverlayMem2Dv2* pBlk, char* filename, const char* title)
{
	int i, j;

	MeasurementTable* pT = new MeasurementTable();

	int cols = pBlk->GetWidth();
	int rows = pBlk->GetHeight();

	pT->Create(cols, rows);
	for (j = 0; j < cols; j++)
	{
		pT->SetHeading(j, "");
		pT->SetDataType(j, MeasurementTable::INT);
	}//end for j...

	pT->SetTitle(title);
	for (i = 0; i < rows; i++)
		for (j = 0; j < cols; j++)
			pT->WriteItem(j, i, pBlk->Read(j, i));

	pT->Save(filename, ",", 1);

	delete pT;
}//end DumpBlock.

/// -------------------------------------------------------------------------------------------------------------------------------------------------
/// Depricated code stored here for amusement.
/// -------------------------------------------------------------------------------------------------------------------------------------------------

/** Transform and Quantise an Inter_16x16 macroblock with a Dmax criterion.
This method provides a speed improvement for macroblock processing and code refactoring. The
QP value is decremented until the mb Lum distortion is below Dmax. The quatised coeff are stored
in the _lumBlk[][], etc. and the inverse reconstructed pels are stored in the _lumBlkTmp[][], etc.
@param pMb	  : Macroblock to transform.
@param Dmax   : Max distortion for this mb.
@param minQP  : Lower limit for the QP descent.
@return			  : Distortion for the mb.
*/
/*
int H264v2Codec::TransAndQuantInter16x16MBlk(MacroBlockH264* pMb, int Dmax, int minQP)
{
int i, j;
int distortion = 0;
int qp = pMb->_mbQP;
int eqp = pMb->_mbEncQP;
if (eqp > (H264V2_MAX_QP + 16))  ///< Only consider lum coeffs.
eqp = H264V2_MAX_QP + 16;

/// ----------------------------- Lum -----------------------------------------------------------
/// Transform without quant and store. This is then used as the starting storage
/// point to reload on each iteration of the Dmax descent. The main blks are only quantised
/// when the final QP value is determined.
_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
_pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
for (i = 0; i < 4; i++)
for (j = 0; j < 4; j++)
{
BlockH264* pb = &(pMb->_lumBlk[i][j]);
/// Make a copy of the input lum blks for later distortion calculations. These input
/// blks enter this method holding the diff between the input and the motion pred images.
pb->Copy(_tmpBlk[i][j].GetBlk());
/// DCT in-place but no quant.
pb->ForwardTransform(_pF4x4TLum);
}//end for i & j...

/// Iterate QP down from qp to either distortion <= Dmax or minQP (without distortion <= Dmax is best
/// that can be done).

do
{
/// Forward.
_pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
/// Inverse
_pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, qp);

/// Copy the Lum blks into the temp Lum blks for forward quant and inverse transform + quant processing.
MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_0_0, MBH264_LUM_3_3);

/// Quant only for the temp Lum blks.
_pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
/// Invese quant and transform.
_pI4x4TLum->SetMode(IInverseTransform::TransformAndQuant);

/// Forward quant and inverse quant and transform. Accumulate distortion.
distortion =0 ;
for (int i = 0; i < 4; i++)
for (int j = 0; j < 4; j++)
{
BlockH264* pb = &(pMb->_lumBlkTmp[i][j]);
pb->ForwardTransform(_pF4x4TLum); ///< Forward quant.

if (eqp > H264V2_MAX_QP) ///< For extended EncQP range [52..67], zero coeffs.
pb->Zero(16 - (eqp - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);

pb->InverseTransform(_pI4x4TLum);	///< Inverse scale, quant and transform.
/// Total square diff with the input store.
#ifdef USE_ABSOLUTE_DIFFERENCE
distortion += pb->GetBlkOverlay()->Tad4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#else
distortion += pb->GetBlkOverlay()->Tsd4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#endif
}//end for i & j...

/// Region of interest multiplier application to the whole mb.
distortion = ROIDistortion(pMb->_mbIndex, distortion);

/// Update mb QP. On exit of the do-while loop these QP values will be selected.
pMb->_mbQP = qp;
pMb->_mbEncQP = eqp;
/// New qp.
if (eqp > H264V2_MAX_QP)
{
eqp -= 4;
if (eqp < H264V2_MAX_QP) eqp = H264V2_MAX_QP;
}//end if eqp...
else
{
qp -= 4; //MbStepSize[qp];
if (qp < minQP) qp = minQP;
}//end else...

NUMITERATIONS++;

} while ((qp > minQP) && (distortion > Dmax)); //end do while qp...

/// Finally quant the lum blks.
for (i = 0; i < 4; i++)
for (j = 0; j < 4; j++)
{
pMb->_lumBlk[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
/// Zero coeffs for extended EncQP range.
if (pMb->_mbEncQP > H264V2_MAX_QP)
pMb->_lumBlk[i][j].Zero(16 - (pMb->_mbEncQP - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);
}//end for i & j...

/// ----------------------------- Chr -----------------------------------------------------------
/// Chr is processed after the QP value for this block has been determined by the Lum process.
int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);
/// Forward.
_pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
_pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
/// Inverse.
_pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
_pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

short* pDcCbBlk = pMb->_cbDcBlk.GetBlk();
short* pDcCrBlk = pMb->_crDcBlk.GetBlk();
for (i = 0; i < 2; i++)
for (j = 0; j < 2; j++)
{
BlockH264*  pCb = &(pMb->_cbBlk[i][j]);
BlockH264*  pCr = &(pMb->_crBlk[i][j]);

_pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
/// Cb
pCb->ForwardTransform(_pF4x4TChr);  ///< DCT.
*pDcCbBlk++ = pCb->GetDC();         ///< Set DC blk
pCb->SetDC(0);                      ///< Clear DC term in AC blk.

/// Cr
pCr->ForwardTransform(_pF4x4TChr);  ///< DCT.
*pDcCrBlk++ = pCr->GetDC();         ///< Set DC blk
pCr->SetDC(0);                      ///< Clear DC term in AC blk.

_pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
pCb->Quantise(_pF4x4TChr);         ///< Scale and quant.
pCr->Quantise(_pF4x4TChr);

/// Follow the lum extended QP range on the Chr AC coeffs.
if (pMb->_mbEncQP > H264V2_MAX_QP)
{
pCb->Zero(16 - (pMb->_mbEncQP - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);
pCr->Zero(16 - (pMb->_mbEncQP - H264V2_MAX_QP), CAVLCH264Impl::zigZag4x4Pos);
}//end if _mbEncQP...
}//end for i & j...

/// Transform and quant the DC blocks.
pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

/// Copy the Chr blks into the temp blks for inverse processing.
MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_CB_DC, MBH264_CR_1_1);

/// Inverse quant and inverse transform the DC blks.
pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

/// Inverse quant then add DC term before inverse transform.
short* pDcCbBlkTmp = pMb->_cbDcBlkTmp.GetBlk();
short* pDcCrBlkTmp = pMb->_crDcBlkTmp.GetBlk();
for (i = 0; i < 2; i++)
for (j = 0; j < 2; j++)
{
BlockH264*  pCbT = &(pMb->_cbBlkTmp[i][j]);
BlockH264*  pCrT = &(pMb->_crBlkTmp[i][j]);

_pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
pCbT->InverseQuantise(_pI4x4TChr);  ///< Inverse scale and quant.
pCbT->SetDC(*pDcCbBlkTmp++);        ///< Load the DC term from the DC blk.
pCrT->InverseQuantise(_pI4x4TChr);
pCrT->SetDC(*pDcCrBlkTmp++);

_pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
pCbT->InverseTransform(_pI4x4TChr);  ///< Inverse transform.
pCrT->InverseTransform(_pI4x4TChr);
}//end for i & j...


return(distortion);
}//end TransAndQuantInter16x16MBlk.
*/
/** Transform and Quantise an Intra_16x16 macroblock to below a max distortion criterion.
This overload method decrements the mb QP value from its existing value until the mb distortion
drops below the specified Dmax value. It returns the distortion at the final value of QP that
meets the criterion and places the decoded mb values in the temp blks. The final inverse decoded
blks are stored in the tmp blks.
@param pMb	  : Macroblock to transform.
@param Dmax   : Target max distortion criterion.
@param minQP  : Lowest QP value allowed.
@return			  : Lum distortion.
*/
/*
int H264v2Codec::TransAndQuantIntra16x16MBlk(MacroBlockH264* pMb, int Dmax, int minQP)
{
  int i, j;
  int qp = pMb->_mbQP;
  int distortion = 0;

  ///------------ Lum in raster scan order--------------------------

  /// Transform without quant to extract the DC term. This is then used as the starting storage
  /// point to reload on each iteration of the Dmax descent. The main blks are only quantised 
  /// when the final QP value is determined.
  _pF4x4TLum->SetMode(IForwardTransform::TransformOnly);
  short* pDcLumBlk = pMb->_lumDcBlk.GetBlk();  ///< Short cut to DC blk.
  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
    {
      BlockH264* pb = &(pMb->_lumBlk[i][j]);

      /// Make a copy of the input lum blks for later distortion calculations. These input
      /// blks enter this method holding the diff between the input and the pred images.
      pb->Copy(_tmpBlk[i][j].GetBlk());
      /// DCT in-place but no quant.
      pb->ForwardTransform(_pF4x4TLum);   ///< DCT.
      *pDcLumBlk++ = pb->GetDC();         ///< Set DC blk
      pb->SetDC(0);                       ///< Clear DC term in AC blk.
    }//end for i & j...

     /// Transform the DC blocks.
  _pFDC4x4T->SetMode(IForwardTransform::TransformOnly);
  pMb->_lumDcBlk.ForwardTransform(_pFDC4x4T);

  /// Iterate QP down from qp to either distortion <= Dmax or minQP (without distortion <= Dmax is best 
  /// that can be done).
  do
  {
    /// Forward.
    _pF4x4TLum->SetParameter(IForwardTransform::QUANT_ID, qp);
    _pFDC4x4T->SetParameter(IForwardTransform::QUANT_ID, qp);
    /// Inverse
    _pI4x4TLum->SetParameter(IInverseTransform::QUANT_ID, qp);
    _pIDC4x4T->SetParameter(IInverseTransform::QUANT_ID, qp);

    /// Copy the Lum blks into the temp Lum blks for forward quant and inverse transform + quant processing.
    MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_LUM_DC, MBH264_LUM_3_3);

    /// Quant the temp Lum blks.
    _pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
    for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
      {
        pMb->_lumBlkTmp[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
      }//end for i & j...

       /// Quant the temp DC blocks.
    _pFDC4x4T->SetMode(IForwardTransform::QuantOnly);
    pMb->_lumDcBlkTmp.ForwardTransform(_pFDC4x4T);

    /// For QP = H264V2_MAX_QP it is likely that all coeffs are zeros. Find the QP value 
    /// that results in at least 1 non-zero coeff.
    //    if(qp == H264V2_MAX_QP)
    //      qp = GetQPtoNonZeroCoeff(pMb, minQP);

    /// Inverse quant and inverse transform the DC blks.
    pMb->_lumDcBlkTmp.InverseTransform(_pIDC4x4T);

    /// Inverse quant then add DC term before inverse transform. Accumulate distortion.
    distortion = 0;
    short* pDcLumBlkTmp = pMb->_lumDcBlkTmp.GetBlk();  ///< Short cut to DC blk.
    for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
      {
        BlockH264* pb = &(pMb->_lumBlkTmp[i][j]);

        _pI4x4TLum->SetMode(IInverseTransform::QuantOnly);
        pb->InverseQuantise(_pI4x4TLum);	///< Inverse scale and quant.
        pb->SetDC(*pDcLumBlkTmp++);       ///< Load the DC term from the DC blk.

        _pI4x4TLum->SetMode(IInverseTransform::TransformOnly);
        pb->InverseTransform(_pI4x4TLum); ///< Inverse transform.

                                          /// Total square diff with the input store.
#ifdef USE_ABSOLUTE_DIFFERENCE
        distortion += pb->GetBlkOverlay()->Tad4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#else
        distortion += pb->GetBlkOverlay()->Tsd4x4(*(_tmpBlk[i][j].GetBlkOverlay()));
#endif
      }//end for i & j...

       /// Region of interest multiplier application to the whole mb.
    distortion = ROIDistortion(pMb->_mbIndex, distortion);

    /// Update mb QP.
    pMb->_mbQP = qp;

    /// New qp.
    qp -= 4; //MbStepSize[qp];
    if (qp < minQP) qp = minQP;

  } while ((qp > minQP) && (distortion > Dmax)); //end do while qp...

                                                 /// Finally quant the main lum AC and DC blks.
  _pF4x4TLum->SetMode(IForwardTransform::QuantOnly);
  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
    {
      pMb->_lumBlk[i][j].Quantise(_pF4x4TLum);        ///< Scale and quant.
    }//end for i & j...
  _pFDC4x4T->SetMode(IForwardTransform::QuantOnly);
  pMb->_lumDcBlk.ForwardTransform(_pFDC4x4T);

  ///----------- Chr in raster scan order --------------------------

  int mbChrQP = MacroBlockH264::GetQPc(pMb->_mbQP);
  /// Forward.
  _pF4x4TChr->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
  _pFDC2x2T->SetParameter(IForwardTransform::QUANT_ID, mbChrQP);
  /// Inverse.
  _pI4x4TChr->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);
  _pIDC2x2T->SetParameter(IInverseTransform::QUANT_ID, mbChrQP);

  short* pDcCbBlk = pMb->_cbDcBlk.GetBlk();
  short* pDcCrBlk = pMb->_crDcBlk.GetBlk();
  for (i = 0; i < 2; i++)
    for (j = 0; j < 2; j++)
    {
      BlockH264*  pCb = &(pMb->_cbBlk[i][j]);
      BlockH264*  pCr = &(pMb->_crBlk[i][j]);

      _pF4x4TChr->SetMode(IForwardTransform::TransformOnly);
      /// Cb
      pCb->ForwardTransform(_pF4x4TChr);  ///< DCT.
      *pDcCbBlk++ = pCb->GetDC();         ///< Set DC blk
      pCb->SetDC(0);                      ///< Clear DC term in AC blk.

                                          /// Cr
      pCr->ForwardTransform(_pF4x4TChr);  ///< DCT.
      *pDcCrBlk++ = pCr->GetDC();         ///< Set DC blk
      pCr->SetDC(0);                      ///< Clear DC term in AC blk.

      _pF4x4TChr->SetMode(IForwardTransform::QuantOnly);
      pCb->Quantise(_pF4x4TChr);         ///< Scale and quant.
      pCr->Quantise(_pF4x4TChr);
    }//end for i & j...

     /// Transform and quant the DC blocks.
  pMb->_cbDcBlk.ForwardTransform(_pFDC2x2T);
  pMb->_crDcBlk.ForwardTransform(_pFDC2x2T);

  /// Copy the Chr blks into the temp blks for inverse processing.
  MacroBlockH264::CopyBlksToTmpBlks(pMb, MBH264_CB_DC, MBH264_CR_1_1);

  /// Inverse quant and inverse transform the DC blks.
  pMb->_cbDcBlkTmp.InverseTransform(_pIDC2x2T);
  pMb->_crDcBlkTmp.InverseTransform(_pIDC2x2T);

  /// Inverse quant then add DC term before inverse transform.
  short* pDcCbBlkTmp = pMb->_cbDcBlkTmp.GetBlk();
  short* pDcCrBlkTmp = pMb->_crDcBlkTmp.GetBlk();
  for (i = 0; i < 2; i++)
    for (j = 0; j < 2; j++)
    {
      BlockH264*  pCbT = &(pMb->_cbBlkTmp[i][j]);
      BlockH264*  pCrT = &(pMb->_crBlkTmp[i][j]);

      _pI4x4TChr->SetMode(IInverseTransform::QuantOnly);
      pCbT->InverseQuantise(_pI4x4TChr);  ///< Inverse scale and quant.
      pCbT->SetDC(*pDcCbBlkTmp++);        ///< Load the DC term from the DC blk.
      pCrT->InverseQuantise(_pI4x4TChr);
      pCrT->SetDC(*pDcCrBlkTmp++);

      _pI4x4TChr->SetMode(IInverseTransform::TransformOnly);
      pCbT->InverseTransform(_pI4x4TChr);  ///< Inverse transform.
      pCrT->InverseTransform(_pI4x4TChr);
    }//end for i & j...

     /// Restore state.
  _pFDC4x4T->SetMode(IForwardTransform::TransformAndQuant);
  pMb->_mbEncQP = pMb->_mbQP;

  return(distortion);
}//end TransAndQuantIntra16x16MBlk.

*/