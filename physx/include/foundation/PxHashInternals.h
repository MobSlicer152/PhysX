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

#ifndef PX_HASH_INTERNALS_H
#define PX_HASH_INTERNALS_H

#include "foundation/PxAllocator.h"
#include "foundation/PxBitUtils.h"
#include "foundation/PxMathIntrinsics.h"
#include "foundation/PxBasicTemplates.h"
#include "foundation/PxHash.h"

#if PX_VC
#pragma warning(push)
#pragma warning(disable : 4127) // conditional expression is constant
#endif
#if !PX_DOXYGEN
namespace physx
{
#endif
template <class Entry, class Key, class HashFn, class GetKey, class PxAllocator, bool compacting>
class PxHashBase : private PxAllocator
{
	void init(PxU32 initialTableSize, float loadFactor)
	{
		mBuffer = NULL;
		mEntries = NULL;
		mEntriesNext = NULL;
		mHash = NULL;
		mEntriesCapacity = 0;
		mHashSize = 0;
		mLoadFactor = loadFactor;
		mFreeList = PxU32(EOL);
		mTimestamp = 0;
		mEntriesCount = 0;

		if(initialTableSize)
			reserveInternal(initialTableSize);
	}

  public:
	typedef Entry EntryType;

	PxHashBase(PxU32 initialTableSize = 64, float loadFactor = 0.75f) : PxAllocator("hashBase")
	{
		init(initialTableSize, loadFactor);
	}

	PxHashBase(PxU32 initialTableSize, float loadFactor, const PxAllocator& alloc) : PxAllocator(alloc)
	{
		init(initialTableSize, loadFactor);
	}

	PxHashBase(const PxAllocator& alloc) : PxAllocator(alloc)
	{
		init(64, 0.75f);
	}

	~PxHashBase()
	{
		destroy(); // No need to clear()

		if(mBuffer)
			PxAllocator::deallocate(mBuffer);
	}

	static const PxU32 EOL = 0xffffffff;

	PX_INLINE Entry* create(const Key& k, bool& exists)
	{
		PxU32 h = 0;
		if(mHashSize)
		{
			h = hash(k);
			PxU32 index = mHash[h];
			while(index != EOL && !HashFn().equal(GetKey()(mEntries[index]), k))
				index = mEntriesNext[index];
			exists = index != EOL;
			if(exists)
				return mEntries + index;
		}
		else
			exists = false;

		if(freeListEmpty())
		{
			grow();
			h = hash(k);
		}

		PxU32 entryIndex = freeListGetNext();

		mEntriesNext[entryIndex] = mHash[h];
		mHash[h] = entryIndex;

		mEntriesCount++;
		mTimestamp++;

		return mEntries + entryIndex;
	}

	PX_INLINE const Entry* find(const Key& k) const
	{
		if(!mEntriesCount)
			return NULL;

		const PxU32 h = hash(k);
		PxU32 index = mHash[h];
		while(index != EOL && !HashFn().equal(GetKey()(mEntries[index]), k))
			index = mEntriesNext[index];
		return index != EOL ? mEntries + index : NULL;
	}

	PX_INLINE bool erase(const Key& k, Entry& e)
	{
		if(!mEntriesCount)
			return false;

		const PxU32 h = hash(k);
		PxU32* ptr = mHash + h;
		while(*ptr != EOL && !HashFn().equal(GetKey()(mEntries[*ptr]), k))
			ptr = mEntriesNext + *ptr;

		if(*ptr == EOL)
			return false;

		PX_PLACEMENT_NEW(&e, Entry)(mEntries[*ptr]);		

		return eraseInternal(ptr);
	}

	PX_INLINE bool erase(const Key& k)
	{
		if(!mEntriesCount)
			return false;

		const PxU32 h = hash(k);
		PxU32* ptr = mHash + h;
		while(*ptr != EOL && !HashFn().equal(GetKey()(mEntries[*ptr]), k))
			ptr = mEntriesNext + *ptr;

		if(*ptr == EOL)
			return false;		

		return eraseInternal(ptr);
	}

	PX_INLINE PxU32 size() const
	{
		return mEntriesCount;
	}

	PX_INLINE PxU32 capacity() const
	{
		return mHashSize;
	}

	void clear()
	{
		if(!mHashSize || mEntriesCount == 0)
			return;

		destroy();

		intrinsics::memSet(mHash, EOL, mHashSize * sizeof(PxU32));

		const PxU32 sizeMinus1 = mEntriesCapacity - 1;
		for(PxU32 i = 0; i < sizeMinus1; i++)
		{
			PxPrefetchLine(mEntriesNext + i, 128);
			mEntriesNext[i] = i + 1;
		}
		mEntriesNext[mEntriesCapacity - 1] = PxU32(EOL);
		mFreeList = 0;
		mEntriesCount = 0;
	}

	void reserve(PxU32 size)
	{
		if(size > mHashSize)
			reserveInternal(size);
	}

	PX_INLINE const Entry* getEntries() const
	{
		return mEntries;
	}

	PX_INLINE Entry* insertUnique(const Key& k)
	{
		PX_ASSERT(find(k) == NULL);
		PxU32 h = hash(k);

		PxU32 entryIndex = freeListGetNext();

		mEntriesNext[entryIndex] = mHash[h];
		mHash[h] = entryIndex;

		mEntriesCount++;
		mTimestamp++;

		return mEntries + entryIndex;
	}

  private:
	void destroy()
	{
		for(PxU32 i = 0; i < mHashSize; i++)
		{
			for(PxU32 j = mHash[i]; j != EOL; j = mEntriesNext[j])
				mEntries[j].~Entry();
		}
	}

	template <typename HK, typename GK, class A, bool comp>
	PX_NOINLINE void copy(const PxHashBase<Entry, Key, HK, GK, A, comp>& other);

	// free list management - if we're coalescing, then we use mFreeList to hold
	// the top of the free list and it should always be equal to size(). Otherwise,
	// we build a free list in the next() pointers.

	PX_INLINE void freeListAdd(PxU32 index)
	{
		if(compacting)
		{
			mFreeList--;
			PX_ASSERT(mFreeList == mEntriesCount);
		}
		else
		{
			mEntriesNext[index] = mFreeList;
			mFreeList = index;
		}
	}

	PX_INLINE void freeListAdd(PxU32 start, PxU32 end)
	{
		if(!compacting)
		{
			for(PxU32 i = start; i < end - 1; i++) // add the new entries to the free list
				mEntriesNext[i] = i + 1;

			// link in old free list
			mEntriesNext[end - 1] = mFreeList;
			PX_ASSERT(mFreeList != end - 1);
			mFreeList = start;
		}
		else if(mFreeList == EOL) // don't reset the free ptr for the compacting hash unless it's empty
			mFreeList = start;
	}

	PX_INLINE PxU32 freeListGetNext()
	{
		PX_ASSERT(!freeListEmpty());
		if(compacting)
		{
			PX_ASSERT(mFreeList == mEntriesCount);
			return mFreeList++;
		}
		else
		{
			PxU32 entryIndex = mFreeList;
			mFreeList = mEntriesNext[mFreeList];
			return entryIndex;
		}
	}

	PX_INLINE bool freeListEmpty() const
	{
		if(compacting)
			return mEntriesCount == mEntriesCapacity;
		else
			return mFreeList == EOL;
	}

	PX_INLINE void replaceWithLast(PxU32 index)
	{
		PX_PLACEMENT_NEW(mEntries + index, Entry)(mEntries[mEntriesCount]);
		mEntries[mEntriesCount].~Entry();
		mEntriesNext[index] = mEntriesNext[mEntriesCount];

		PxU32 h = hash(GetKey()(mEntries[index]));
		PxU32* ptr;
		for(ptr = mHash + h; *ptr != mEntriesCount; ptr = mEntriesNext + *ptr)
			PX_ASSERT(*ptr != EOL);
		*ptr = index;
	}

	PX_INLINE PxU32 hash(const Key& k, PxU32 hashSize) const
	{
		return HashFn()(k) & (hashSize - 1);
	}

	PX_INLINE PxU32 hash(const Key& k) const
	{
		return hash(k, mHashSize);
	}

	PX_INLINE bool eraseInternal(PxU32* ptr)
	{
		const PxU32 index = *ptr;

		*ptr = mEntriesNext[index];

		mEntries[index].~Entry();

		mEntriesCount--;
		mTimestamp++;

		if (compacting && index != mEntriesCount)
			replaceWithLast(index);

		freeListAdd(index);
		return true;
	}

	PX_NOINLINE void reserveInternal(PxU32 size)
	{
		if(!PxIsPowerOfTwo(size))
			size = PxNextPowerOfTwo(size);

		PX_ASSERT(!(size & (size - 1)));

		// decide whether iteration can be done on the entries directly
		bool resizeCompact = compacting || freeListEmpty();

		// define new table sizes
		PxU32 oldEntriesCapacity = mEntriesCapacity;
		PxU32 newEntriesCapacity = PxU32(float(size) * mLoadFactor);
		PxU32 newHashSize = size;

		// allocate new common buffer and setup pointers to new tables
		uint8_t* newBuffer;
		PxU32* newHash;
		PxU32* newEntriesNext;
		Entry* newEntries;
		{
			PxU32 newHashByteOffset = 0;
			PxU32 newEntriesNextBytesOffset = newHashByteOffset + newHashSize * sizeof(PxU32);
			PxU32 newEntriesByteOffset = newEntriesNextBytesOffset + newEntriesCapacity * sizeof(PxU32);
			newEntriesByteOffset += (16 - (newEntriesByteOffset & 15)) & 15;
			PxU32 newBufferByteSize = newEntriesByteOffset + newEntriesCapacity * sizeof(Entry);

			newBuffer = reinterpret_cast<uint8_t*>(PxAllocator::allocate(newBufferByteSize, __FILE__, __LINE__));
			PX_ASSERT(newBuffer);

			newHash = reinterpret_cast<PxU32*>(newBuffer + newHashByteOffset);
			newEntriesNext = reinterpret_cast<PxU32*>(newBuffer + newEntriesNextBytesOffset);
			newEntries = reinterpret_cast<Entry*>(newBuffer + newEntriesByteOffset);
		}

		// initialize new hash table
		intrinsics::memSet(newHash, PxU32(EOL), newHashSize * sizeof(PxU32));

		// iterate over old entries, re-hash and create new entries
		if(resizeCompact)
		{
			// check that old free list is empty - we don't need to copy the next entries
			PX_ASSERT(compacting || mFreeList == EOL);

			for(PxU32 index = 0; index < mEntriesCount; ++index)
			{
				PxU32 h = hash(GetKey()(mEntries[index]), newHashSize);
				newEntriesNext[index] = newHash[h];
				newHash[h] = index;

				PX_PLACEMENT_NEW(newEntries + index, Entry)(mEntries[index]);
				mEntries[index].~Entry();
			}
		}
		else
		{
			// copy old free list, only required for non compact resizing
			intrinsics::memCopy(newEntriesNext, mEntriesNext, mEntriesCapacity * sizeof(PxU32));

			for(PxU32 bucket = 0; bucket < mHashSize; bucket++)
			{
				PxU32 index = mHash[bucket];
				while(index != EOL)
				{
					PxU32 h = hash(GetKey()(mEntries[index]), newHashSize);
					newEntriesNext[index] = newHash[h];
					PX_ASSERT(index != newHash[h]);

					newHash[h] = index;

					PX_PLACEMENT_NEW(newEntries + index, Entry)(mEntries[index]);
					mEntries[index].~Entry();

					index = mEntriesNext[index];
				}
			}
		}

		// swap buffer and pointers
		PxAllocator::deallocate(mBuffer);
		mBuffer = newBuffer;
		mHash = newHash;
		mHashSize = newHashSize;
		mEntriesNext = newEntriesNext;
		mEntries = newEntries;
		mEntriesCapacity = newEntriesCapacity;

		freeListAdd(oldEntriesCapacity, newEntriesCapacity);
	}

	void grow()
	{
		PX_ASSERT((mFreeList == EOL) || (compacting && (mEntriesCount == mEntriesCapacity)));

		PxU32 size = mHashSize == 0 ? 16 : mHashSize * 2;
		reserve(size);
	}

	uint8_t* mBuffer;
	Entry* mEntries;
	PxU32* mEntriesNext; // same size as mEntries
	PxU32* mHash;
	PxU32 mEntriesCapacity;
	PxU32 mHashSize;
	float mLoadFactor;
	PxU32 mFreeList;
	PxU32 mTimestamp;
	PxU32 mEntriesCount; // number of entries

  public:
	class Iter
	{
	  public:
		PX_INLINE Iter(PxHashBase& b) : mBucket(0), mEntry(PxU32(b.EOL)), mTimestamp(b.mTimestamp), mBase(b)
		{
			if(mBase.mEntriesCapacity > 0)
			{
				mEntry = mBase.mHash[0];
				skip();
			}
		}

		PX_INLINE void check() const
		{
			PX_ASSERT(mTimestamp == mBase.mTimestamp);
		}
		PX_INLINE const Entry& operator*() const
		{
			check();
			return mBase.mEntries[mEntry];
		}
		PX_INLINE Entry& operator*()
		{
			check();
			return mBase.mEntries[mEntry];
		}
		PX_INLINE const Entry* operator->() const
		{
			check();
			return mBase.mEntries + mEntry;
		}
		PX_INLINE Entry* operator->()
		{
			check();
			return mBase.mEntries + mEntry;
		}
		PX_INLINE Iter operator++()
		{
			check();
			advance();
			return *this;
		}
		PX_INLINE Iter operator++(int)
		{
			check();
			Iter i = *this;
			advance();
			return i;
		}
		PX_INLINE bool done() const
		{
			check();
			return mEntry == mBase.EOL;
		}

	  private:
		PX_INLINE void advance()
		{
			mEntry = mBase.mEntriesNext[mEntry];
			skip();
		}
		PX_INLINE void skip()
		{
			while(mEntry == mBase.EOL)
			{
				if(++mBucket == mBase.mHashSize)
					break;
				mEntry = mBase.mHash[mBucket];
			}
		}

		Iter& operator=(const Iter&);

		PxU32 mBucket;
		PxU32 mEntry;
		PxU32 mTimestamp;
		PxHashBase& mBase;
	};

	/*!
	Iterate over entries in a hash base and allow entry erase while iterating
	*/
	class PxEraseIterator
	{
	public:
		PX_INLINE PxEraseIterator(PxHashBase& b): mBase(b)
		{
			reset();
		}

		PX_INLINE Entry* eraseCurrentGetNext(bool eraseCurrent)
		{
			if(eraseCurrent && mCurrentEntryIndexPtr)
			{
				mBase.eraseInternal(mCurrentEntryIndexPtr);
				// if next was valid return the same ptr, if next was EOL search new hash entry
				if(*mCurrentEntryIndexPtr != mBase.EOL)
					return mBase.mEntries + *mCurrentEntryIndexPtr;
				else
					return traverseHashEntries();
			}

			// traverse mHash to find next entry
			if(mCurrentEntryIndexPtr == NULL)
				return traverseHashEntries();
			
			const PxU32 index = *mCurrentEntryIndexPtr;			
			if(mBase.mEntriesNext[index] == mBase.EOL)
			{
				return traverseHashEntries();
			}
			else
			{
				mCurrentEntryIndexPtr = mBase.mEntriesNext + index;
				return mBase.mEntries + *mCurrentEntryIndexPtr;
			}
		}

		PX_INLINE void reset()
		{
			mCurrentHashIndex = 0;
			mCurrentEntryIndexPtr = NULL;			
		}

	private:
		PX_INLINE Entry* traverseHashEntries()
		{
			mCurrentEntryIndexPtr = NULL;			
			while (mCurrentEntryIndexPtr == NULL && mCurrentHashIndex < mBase.mHashSize)
			{
				if (mBase.mHash[mCurrentHashIndex] != mBase.EOL)
				{
					mCurrentEntryIndexPtr = mBase.mHash + mCurrentHashIndex;
					mCurrentHashIndex++;
					return mBase.mEntries + *mCurrentEntryIndexPtr;
				}
				else
				{
					mCurrentHashIndex++;
				}
			}
			return NULL;
		}

		PxEraseIterator& operator=(const PxEraseIterator&);
	private:
		PxU32*	mCurrentEntryIndexPtr;
		PxU32	mCurrentHashIndex;		
		PxHashBase&	mBase;
	};
};

template <class Entry, class Key, class HashFn, class GetKey, class PxAllocator, bool compacting>
template <typename HK, typename GK, class A, bool comp>
PX_NOINLINE void
PxHashBase<Entry, Key, HashFn, GetKey, PxAllocator, compacting>::copy(const PxHashBase<Entry, Key, HK, GK, A, comp>& other)
{
	reserve(other.mEntriesCount);

	for(PxU32 i = 0; i < other.mEntriesCount; i++)
	{
		for(PxU32 j = other.mHash[i]; j != EOL; j = other.mEntriesNext[j])
		{
			const Entry& otherEntry = other.mEntries[j];

			bool exists;
			Entry* newEntry = create(GK()(otherEntry), exists);
			PX_ASSERT(!exists);

			PX_PLACEMENT_NEW(newEntry, Entry)(otherEntry);
		}
	}
}

template <class Key, class HashFn, class PxAllocator = typename PxAllocatorTraits<Key>::Type, bool Coalesced = false>
class PxHashSetBase
{
	PX_NOCOPY(PxHashSetBase)
  public:
	struct GetKey
	{
		PX_INLINE const Key& operator()(const Key& e)
		{
			return e;
		}
	};

	typedef PxHashBase<Key, Key, HashFn, GetKey, PxAllocator, Coalesced> BaseMap;
	typedef typename BaseMap::Iter Iterator;

	PxHashSetBase(PxU32 initialTableSize, float loadFactor, const PxAllocator& alloc)
	: mBase(initialTableSize, loadFactor, alloc)
	{
	}

	PxHashSetBase(const PxAllocator& alloc) : mBase(64, 0.75f, alloc)
	{
	}

	PxHashSetBase(PxU32 initialTableSize = 64, float loadFactor = 0.75f) : mBase(initialTableSize, loadFactor)
	{
	}

	bool insert(const Key& k)
	{
		bool exists;
		Key* e = mBase.create(k, exists);
		if(!exists)
			PX_PLACEMENT_NEW(e, Key)(k);
		return !exists;
	}

	PX_INLINE bool contains(const Key& k) const
	{
		return mBase.find(k) != 0;
	}
	PX_INLINE bool erase(const Key& k)
	{
		return mBase.erase(k);
	}
	PX_INLINE PxU32 size() const
	{
		return mBase.size();
	}
	PX_INLINE PxU32 capacity() const
	{
		return mBase.capacity();
	}
	PX_INLINE void reserve(PxU32 size)
	{
		mBase.reserve(size);
	}
	PX_INLINE void clear()
	{
		mBase.clear();
	}

  protected:
	BaseMap mBase;
};

template <class Key, class Value, class HashFn, class PxAllocator = typename PxAllocatorTraits<PxPair<const Key, Value> >::Type>
class PxHashMapBase
{
	PX_NOCOPY(PxHashMapBase)
  public:
	typedef PxPair<const Key, Value> Entry;

	struct GetKey
	{
		PX_INLINE const Key& operator()(const Entry& e)
		{
			return e.first;
		}
	};

	typedef PxHashBase<Entry, Key, HashFn, GetKey, PxAllocator, true> BaseMap;
	typedef typename BaseMap::Iter Iterator;
	typedef typename BaseMap::PxEraseIterator EraseIterator;

	PxHashMapBase(PxU32 initialTableSize, float loadFactor, const PxAllocator& alloc)
	: mBase(initialTableSize, loadFactor, alloc)
	{
	}

	PxHashMapBase(const PxAllocator& alloc) : mBase(64, 0.75f, alloc)
	{
	}

	PxHashMapBase(PxU32 initialTableSize = 64, float loadFactor = 0.75f) : mBase(initialTableSize, loadFactor)
	{
	}

	bool insert(const Key /*&*/ k, const Value /*&*/ v)
	{
		bool exists;
		Entry* e = mBase.create(k, exists);
		if(!exists)
			PX_PLACEMENT_NEW(e, Entry)(k, v);
		return !exists;
	}

	Value& operator[](const Key& k)
	{
		bool exists;
		Entry* e = mBase.create(k, exists);
		if(!exists)
			PX_PLACEMENT_NEW(e, Entry)(k, Value());

		return e->second;
	}

	PX_INLINE const Entry* find(const Key& k) const
	{
		return mBase.find(k);
	}
	PX_INLINE bool erase(const Key& k)
	{
		return mBase.erase(k);
	}
	PX_INLINE bool erase(const Key& k, Entry& e)
	{		
		return mBase.erase(k, e);
	}
	PX_INLINE PxU32 size() const
	{
		return mBase.size();
	}
	PX_INLINE PxU32 capacity() const
	{
		return mBase.capacity();
	}
	PX_INLINE Iterator getIterator()
	{
		return Iterator(mBase);
	}
	PX_INLINE EraseIterator getEraseIterator()
	{
		return EraseIterator(mBase);
	}
	PX_INLINE void reserve(PxU32 size)
	{
		mBase.reserve(size);
	}
	PX_INLINE void clear()
	{
		mBase.clear();
	}

  protected:
	BaseMap mBase;
};
#if !PX_DOXYGEN
} // namespace physx
#endif

#if PX_VC
#pragma warning(pop)
#endif
#endif

