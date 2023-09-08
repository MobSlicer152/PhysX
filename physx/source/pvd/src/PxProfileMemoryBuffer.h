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

#ifndef PX_PROFILE_MEMORY_BUFFER_H
#define PX_PROFILE_MEMORY_BUFFER_H

#include "foundation/PxAllocator.h"
#include "foundation/PxMemory.h"

namespace physx { namespace profile {

	template<typename TAllocator = typename PxAllocatorTraits<uint8_t>::Type >
	class MemoryBuffer : public TAllocator
	{
		uint8_t* mBegin;
		uint8_t* mEnd;
		uint8_t* mCapacityEnd;

	public:
		MemoryBuffer( const TAllocator& inAlloc = TAllocator() ) : TAllocator( inAlloc ), mBegin( 0 ), mEnd( 0 ), mCapacityEnd( 0 ) {}
		~MemoryBuffer()
		{
			if ( mBegin ) TAllocator::deallocate( mBegin );
		}
		PxU32 size() const { return static_cast<PxU32>( mEnd - mBegin ); }
		PxU32 capacity() const { return static_cast<PxU32>( mCapacityEnd - mBegin ); }
		uint8_t* begin() { return mBegin; }
		uint8_t* end() { return mEnd; }
		void setEnd(uint8_t* nEnd) { mEnd = nEnd; }
		const uint8_t* begin() const { return mBegin; }
		const uint8_t* end() const { return mEnd; }
		void clear() { mEnd = mBegin; }
		PxU32 write( uint8_t inValue )
		{
			growBuf( 1 );
			*mEnd = inValue;
			++mEnd;
			return 1;
		}

		template<typename TDataType>
		PxU32 write( const TDataType& inValue )
		{
			PxU32 writtenSize = sizeof(TDataType);
			growBuf(writtenSize);
			const uint8_t* __restrict readPtr = reinterpret_cast< const uint8_t* >( &inValue );
			uint8_t* __restrict writePtr = mEnd;
			for ( PxU32 idx = 0; idx < sizeof(TDataType); ++idx ) writePtr[idx] = readPtr[idx];
			mEnd += writtenSize;
			return writtenSize;
		}
		
		template<typename TDataType>
		PxU32 write( const TDataType* inValue, PxU32 inLength )
		{
			if ( inValue && inLength )
			{
				PxU32 writeSize = inLength * sizeof( TDataType );
				growBuf( writeSize );
				PxMemCopy( mBegin + size(), inValue, writeSize );
				mEnd += writeSize;
				return writeSize;
			}
			return 0;
		}

		// used by atomic write. Store the data and write the end afterwards
		// we dont check the buffer size, it should not resize on the fly
		template<typename TDataType>
		PxU32 write(const TDataType* inValue, PxU32 inLength, PxI32 index)
		{
			if (inValue && inLength)
			{
				PxU32 writeSize = inLength * sizeof(TDataType);
				PX_ASSERT(mBegin + index + writeSize < mCapacityEnd);
				PxMemCopy(mBegin + index, inValue, writeSize);				
				return writeSize;
			}
			return 0;
		}
		
		void growBuf( PxU32 inAmount )
		{
			PxU32 newSize = size() + inAmount;
			reserve( newSize );
		}
		void resize( PxU32 inAmount )
		{
			reserve( inAmount );
			mEnd = mBegin + inAmount;
		}
		void reserve( PxU32 newSize )
		{
			PxU32 currentSize = size();
			if ( newSize >= capacity() )
			{
				const PxU32 allocSize = mBegin ? newSize * 2 : newSize;

				uint8_t* newData = static_cast<uint8_t*>(TAllocator::allocate(allocSize, __FILE__, __LINE__));
				memset(newData, 0xf,allocSize);
				if ( mBegin )
				{
					PxMemCopy( newData, mBegin, currentSize );
					TAllocator::deallocate( mBegin );
				}
				mBegin = newData;
				mEnd = mBegin + currentSize;
				mCapacityEnd = mBegin + allocSize;
			}
		}
	};

	
	class TempMemoryBuffer
	{
		uint8_t* mBegin;
		uint8_t* mEnd;
		uint8_t* mCapacityEnd;

	public:
		TempMemoryBuffer(uint8_t* data, PxI32 size) : mBegin(data), mEnd(data), mCapacityEnd(data + size) {}
		~TempMemoryBuffer()
		{			
		}
		PxU32 size() const { return static_cast<PxU32>(mEnd - mBegin); }
		PxU32 capacity() const { return static_cast<PxU32>(mCapacityEnd - mBegin); }
		const uint8_t* begin() { return mBegin; }
		uint8_t* end() { return mEnd; }
		const uint8_t* begin() const { return mBegin; }
		const uint8_t* end() const { return mEnd; }		
		PxU32 write(uint8_t inValue)
		{			
			*mEnd = inValue;
			++mEnd;
			return 1;
		}

		template<typename TDataType>
		PxU32 write(const TDataType& inValue)
		{
			PxU32 writtenSize = sizeof(TDataType);			
			const uint8_t* __restrict readPtr = reinterpret_cast<const uint8_t*>(&inValue);
			uint8_t* __restrict writePtr = mEnd;
			for (PxU32 idx = 0; idx < sizeof(TDataType); ++idx) writePtr[idx] = readPtr[idx];
			mEnd += writtenSize;
			return writtenSize;
		}

		template<typename TDataType>
		PxU32 write(const TDataType* inValue, PxU32 inLength)
		{
			if (inValue && inLength)
			{
				PxU32 writeSize = inLength * sizeof(TDataType);
				PxMemCopy(mBegin + size(), inValue, writeSize);
				mEnd += writeSize;
				return writeSize;
			}
			return 0;
		}
	};

}}

#endif
