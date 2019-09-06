/** @file

MODULE:
 
TAG: 

FILE NAME: 

DESCRIPTION: This is a utility class to extract the header info of
a valid H264v2 bit stream.

COPYRIGHT: (c)CSIR 2007-2018 all rights reserved

LICENSE: Software License Agreement (BSD License)

RESTRICTIONS: 
Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this 
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, 
this list of conditions and the following disclaimer in the documentation and/or 
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may 
be used to endorse or promote products derived from this software without specific 
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED 
OF THE POSSIBILITY OF SUCH DAMAGE.
===========================================================================

*/

#ifndef _H264V2CODECHEADER_H
#define _H264V2CODECHEADER_H

#pragma once

#include "IStreamHeaderReader.h"
#include "NalHeaderH264.h"

// forward declarations
class IBitStreamReader;
class IVlcDecoder;

/**
---------------------------------------------------------------------------
	Class definition.
---------------------------------------------------------------------------
*/
class H264v2CodecHeader: public IStreamHeaderReader
{
public:
	H264v2CodecHeader();
	virtual ~H264v2CodecHeader();

/// Interface implementation.
public:
	/** Extract the header from the input stream.
	Extract the header and pointers from the input into the
	class members.
	@param pSS		: Stream to extract from.
	@param bitLen	: Bit length of input stream.
	@return				: 1 = success, 0 = failed.
	*/
	int Extract(void* pSS, int bitLen);

  /** Get the header length in bits.
  @return : Length in bits.
  */
  int GetHeaderBitLength(void) { return(0); } ///< Not implemented.

	/** Get the header value by name.
	This is the primary method of getting all header variables and caters 
	for all variants by using strings. Care must be taken when unicode strings
	are used. Typical usage:
		int	val;
		if( !Impl->Get((const unsigned char*)("width"), &val) )
			errorStr = "Header value does not exist";

	@param name		:	A string of the header variable required.
	@param value	:	The header value required.
	@return				: 1 = found, 0 = no header exists with this name.
	*/
	int Get(const char* name, int* value);

	/// Implementation specific access methods.
public:
	/** Get the header info after extraction.
	The data returned is that from the last call to Extract() into the current 
	stream position.
	@return	: Requested header value.
	*/
	int GetPictureCodingType(void)								{ return(_pictureCodingType); }

/// Constants.
public:
  /// Constants for _pictureCodingType.
  static const int Intra          = 0;
  static const int Inter          = 1;
  static const int SequenceParams = 2;
  static const int PictureParams  = 3;

///	Persistant stream header data valid after an Extract() call.
protected:
	/// Internal picture properties. (For now)

	/// Main picture type of I/P/PB-frame or sequence and picture parameter sets.
	int _pictureCodingType;

	/// Global stream reader.
	IBitStreamReader*	_pBitStreamReader;
  /// Hedaer vlc decoders.
	IVlcDecoder*	    _pHeaderUnsignedVlcDec;
	IVlcDecoder*	    _pHeaderSignedVlcDec;

	/// NAL unit definition.
	NalHeaderH264		_nal;

};// end class H264v2CodecHeader.

#endif	// _H264V2CODECHEADER_H
