// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (c) 2008-2023 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#ifndef PSFOUNDATION_PSSWITCHINTRINSICS_H
#define PSFOUNDATION_PSSWITCHINTRINSICS_H

#include "foundation/PxAssert.h"
#include <math.h>

// this file is for internal intrinsics - that is, intrinsics that are used in
// cross platform code but do not appear in the API

#if !(__NX__)
#error "This file should only be included by Switch builds!!"
#endif

#if !PX_DOXYGEN
namespace physx
{
#endif

PX_FORCE_INLINE void PxMemoryBarrier()
{
	__sync_synchronize();
}

/*!
Return the index of the highest set bit. Undefined for zero arg.
*/
PX_INLINE PxU32 PxHighestSetBitUnsafe(PxU32 v)
{

	return PxU32(31 - __builtin_clz(v));
}

/*!
Return the index of the highest set bit. Undefined for zero arg.
*/
PX_INLINE PxU32 PxLowestSetBitUnsafe(PxU32 v)
{
	return PxU32(__builtin_ctz(v));
}

/*!
Returns the index of the highest set bit. Returns 32 for v=0.
*/
PX_INLINE PxU32 PxCountLeadingZeros(PxU32 v)
{
	if(v)
		return PxU32(__builtin_clz(v));
	else
		return 32u;
}

/*!
Prefetch aligned 64B x86, 32b ARM around \c ptr+offset.
*/
PX_FORCE_INLINE void PxPrefetchLine(const void* ptr, PxU32 offset = 0)
{
	__builtin_prefetch(reinterpret_cast<const char* PX_RESTRICT>(ptr) + offset, 0, 3);
}

/*!
Prefetch \c count bytes starting at \c ptr.
*/
PX_FORCE_INLINE void PxPrefetch(const void* ptr, PxU32 count = 1)
{
	const char* cp = reinterpret_cast<const char*>(ptr);
	uint64_t p = size_t(ptr);
	uint64_t startLine = p >> 6, endLine = (p + count - 1) >> 6;
	uint64_t lines = endLine - startLine + 1;
	do
	{
		PxPrefetchLine(cp);
		cp += 64;
	} while(--lines);
}

#if !PX_DOXYGEN
} // namespace physx
#endif

#endif // #ifndef PSFOUNDATION_PSSWITCHINTRINSICS_H
