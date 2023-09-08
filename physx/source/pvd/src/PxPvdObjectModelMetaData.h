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

#ifndef PX_PVD_OBJECT_MODEL_METADATA_H
#define PX_PVD_OBJECT_MODEL_METADATA_H

#include "foundation/PxAssert.h"
#include "PxPvdObjectModelBaseTypes.h"
#include "PxPvdBits.h"

namespace physx
{
namespace pvdsdk
{

class PvdInputStream;
class PvdOutputStream;

struct PropertyDescription
{
	NamespacedName mOwnerClassName;
	PxI32 mOwnerClassId;
	String mName;
	String mSemantic;
	// The datatype this property corresponds to.
	PxI32 mDatatype;
	// The name of the datatype
	NamespacedName mDatatypeName;
	// Scalar or array.
	PropertyType::Enum mPropertyType;
	// No other property under any class has this id, it is DB-unique.
	PxI32 mPropertyId;
	// Offset in bytes into the object's data section where this property starts.
	PxU32 m32BitOffset;
	// Offset in bytes into the object's data section where this property starts.
	PxU32 m64BitOffset;

	PropertyDescription(const NamespacedName& clsName, PxI32 classId, String name, String semantic, PxI32 datatype,
	                    const NamespacedName& datatypeName, PropertyType::Enum propType, PxI32 propId,
	                    PxU32 offset32, PxU32 offset64)
	: mOwnerClassName(clsName)
	, mOwnerClassId(classId)
	, mName(name)
	, mSemantic(semantic)
	, mDatatype(datatype)
	, mDatatypeName(datatypeName)
	, mPropertyType(propType)
	, mPropertyId(propId)
	, m32BitOffset(offset32)
	, m64BitOffset(offset64)
	{
	}
	PropertyDescription()
	: mOwnerClassId(-1)
	, mName("")
	, mSemantic("")
	, mDatatype(-1)
	, mPropertyType(PropertyType::Unknown)
	, mPropertyId(-1)
	, m32BitOffset(0)
	, m64BitOffset(0)

	{
	}

	virtual ~PropertyDescription()
	{
	}
};

struct PtrOffsetType
{
	enum Enum
	{
		UnknownOffset,
		VoidPtrOffset,
		StringOffset
	};
};

struct PtrOffset
{
	PtrOffsetType::Enum mOffsetType;
	PxU32 mOffset;
	PtrOffset(PtrOffsetType::Enum type, PxU32 offset) : mOffsetType(type), mOffset(offset)
	{
	}
	PtrOffset() : mOffsetType(PtrOffsetType::UnknownOffset), mOffset(0)
	{
	}
};

inline PxU32 align(PxU32 offset, PxU32 alignment)
{
	PxU32 startOffset = offset;
	PxU32 alignmentMask = ~(alignment - 1);
	offset = (offset + alignment - 1) & alignmentMask;
	PX_ASSERT(offset >= startOffset && (offset % alignment) == 0);
	(void)startOffset;
	return offset;
}

struct ClassDescriptionSizeInfo
{
	// The size of the data section of this object, padded to alignment.
	PxU32 mByteSize;
	// The last data member goes to here.
	PxU32 mDataByteSize;
	// Alignment in bytes of the data section of this object.
	PxU32 mAlignment;
	// the offsets of string handles in the binary value of this class
	DataRef<PtrOffset> mPtrOffsets;
	ClassDescriptionSizeInfo() : mByteSize(0), mDataByteSize(0), mAlignment(0)
	{
	}
};

struct ClassDescription
{
	NamespacedName mName;
	// No other class has this id, it is DB-unique
	PxI32 mClassId;
	// Only single derivation supported.
	PxI32 mBaseClass;
	// If this class has properties that are of uniform type, then we note that.
	// This means that when deserialization an array of these objects we can just use
	// single function to endian convert the entire mess at once.
	PxI32 mPackedUniformWidth;
	// If this class is composed uniformly of members of a given type
	// Or all of its properties are composed uniformly of members of
	// a give ntype, then this class's packed type is that type.
	// PxTransform's packed type would be float.
	PxI32 mPackedClassType;
	// 0: 32Bit 1: 64Bit
	ClassDescriptionSizeInfo mSizeInfo[2];
	// No further property additions allowed.
	bool mLocked;
	// True when this datatype has an array on it that needs to be
	// separately deleted.
	bool mRequiresDestruction;

	ClassDescription(NamespacedName name, PxI32 id)
	: mName(name)
	, mClassId(id)
	, mBaseClass(-1)
	, mPackedUniformWidth(-1)
	, mPackedClassType(-1)
	, mLocked(false)
	, mRequiresDestruction(false)
	{
	}
	ClassDescription()
	: mClassId(-1), mBaseClass(-1), mPackedUniformWidth(-1), mPackedClassType(-1), mLocked(false), mRequiresDestruction(false)
	{
	}
	virtual ~ClassDescription()
	{
	}

	ClassDescriptionSizeInfo& get32BitSizeInfo()
	{
		return mSizeInfo[0];
	}
	ClassDescriptionSizeInfo& get64BitSizeInfo()
	{
		return mSizeInfo[1];
	}
	PxU32& get32BitSize()
	{
		return get32BitSizeInfo().mByteSize;
	}
	PxU32& get64BitSize()
	{
		return get64BitSizeInfo().mByteSize;
	}

	PxU32 get32BitSize() const
	{
		return mSizeInfo[0].mByteSize;
	}
	const ClassDescriptionSizeInfo& getNativeSizeInfo() const
	{
		return mSizeInfo[(sizeof(void*) >> 2) - 1];
	}
	PxU32 getNativeSize() const
	{
		return getNativeSizeInfo().mByteSize;
	}
};

struct MarshalQueryResult
{
	PxI32 srcType;
	PxI32 dstType;
	// If canMarshal != needsMarshalling we have a problem.
	bool canMarshal;
	bool needsMarshalling;
	// Non null if marshalling is possible.
	TBlockMarshaller marshaller;
	MarshalQueryResult(PxI32 _srcType = -1, PxI32 _dstType = -1, bool _canMarshal = false, bool _needs = false,
	                   TBlockMarshaller _m = NULL)
	: srcType(_srcType), dstType(_dstType), canMarshal(_canMarshal), needsMarshalling(_needs), marshaller(_m)
	{
	}
};

struct PropertyMessageEntry
{
	PropertyDescription mProperty;
	NamespacedName mDatatypeName;
	// datatype of the data in the message.
	PxI32 mDatatypeId;
	// where in the message this property starts.
	PxU32 mMessageOffset;
	// size of this entry object
	PxU32 mByteSize;

	// If the chain of properties doesn't have any array properties this indicates the
	PxU32 mDestByteSize;

	PropertyMessageEntry(PropertyDescription propName, NamespacedName dtypeName, PxI32 dtype, PxU32 messageOff,
	                     PxU32 byteSize, PxU32 destByteSize)
	: mProperty(propName)
	, mDatatypeName(dtypeName)
	, mDatatypeId(dtype)
	, mMessageOffset(messageOff)
	, mByteSize(byteSize)
	, mDestByteSize(destByteSize)
	{
	}
	PropertyMessageEntry() : mDatatypeId(-1), mMessageOffset(0), mByteSize(0), mDestByteSize(0)
	{
	}
};

// Create a struct that defines a subset of the properties on an object.
struct PropertyMessageDescription
{
	NamespacedName mClassName;
	// No other class has this id, it is DB-unique
	PxI32 mClassId;
	NamespacedName mMessageName;
	PxI32 mMessageId;
	DataRef<PropertyMessageEntry> mProperties;
	PxU32 mMessageByteSize;
	// Offsets into the property message where const char* items are.
	DataRef<PxU32> mStringOffsets;
	PropertyMessageDescription(const NamespacedName& nm, PxI32 clsId, const NamespacedName& msgName, PxI32 msgId,
	                           PxU32 msgSize)
	: mClassName(nm), mClassId(clsId), mMessageName(msgName), mMessageId(msgId), mMessageByteSize(msgSize)
	{
	}
	PropertyMessageDescription() : mClassId(-1), mMessageId(-1), mMessageByteSize(0)
	{
	}
	virtual ~PropertyMessageDescription()
	{
	}
};

class StringTable
{
  protected:
	virtual ~StringTable()
	{
	}

  public:
	virtual PxU32 getNbStrs() = 0;
	virtual PxU32 getStrs(const char** outStrs, PxU32 bufLen, PxU32 startIdx = 0) = 0;
	virtual const char* registerStr(const char* str, bool& outAdded) = 0;
	const char* registerStr(const char* str)
	{
		bool ignored;
		return registerStr(str, ignored);
	}
	virtual StringHandle strToHandle(const char* str) = 0;
	virtual const char* handleToStr(PxU32 hdl) = 0;
	virtual void release() = 0;

	static StringTable& create();
};

struct None
{
};

template <typename T>
class Option
{
	T mValue;
	bool mHasValue;

  public:
	Option(const T& val) : mValue(val), mHasValue(true)
	{
	}
	Option(None nothing = None()) : mHasValue(false)
	{
		(void)nothing;
	}
	Option(const Option& other) : mValue(other.mValue), mHasValue(other.mHasValue)
	{
	}
	Option& operator=(const Option& other)
	{
		mValue = other.mValue;
		mHasValue = other.mHasValue;
		return *this;
	}
	bool hasValue() const
	{
		return mHasValue;
	}
	const T& getValue() const
	{
		PX_ASSERT(hasValue());
		return mValue;
	}
	T& getValue()
	{
		PX_ASSERT(hasValue());
		return mValue;
	}
	operator const T&() const
	{
		return getValue();
	}
	operator T&()
	{
		return getValue();
	}
	T* operator->()
	{
		return &getValue();
	}
	const T* operator->() const
	{
		return &getValue();
	}
};

/**
 *	Create new classes and add properties to some existing ones.
 *	The default classes are created already, the simple types
 *  along with the basic math types.
 *	(uint8_t, int8_t, etc )
 *	(PxVec3, PxQuat, PxTransform, PxMat33, PxMat34, PxMat44)
 */
class PvdObjectModelMetaData
{
  protected:
	virtual ~PvdObjectModelMetaData()
	{
	}

  public:
	virtual ClassDescription getOrCreateClass(const NamespacedName& nm) = 0;
	// get or create parent, lock parent. deriveFrom getOrCreatechild.
	virtual bool deriveClass(const NamespacedName& parent, const NamespacedName& child) = 0;
	virtual Option<ClassDescription> findClass(const NamespacedName& nm) const = 0;
	template <typename TDataType>
	Option<ClassDescription> findClass()
	{
		return findClass(getPvdNamespacedNameForType<TDataType>());
	}
	virtual Option<ClassDescription> getClass(PxI32 classId) const = 0;
	virtual ClassDescription* getClassPtr(PxI32 classId) const = 0;

	virtual Option<ClassDescription> getParentClass(PxI32 classId) const = 0;
	bool isDerivedFrom(PxI32 classId, PxI32 parentClass) const
	{
		if(classId == parentClass)
			return true;
		ClassDescription* p = getClassPtr(getClassPtr(classId)->mBaseClass);
		while(p != NULL)
		{
			if(p->mClassId == parentClass)
				return true;
			p = getClassPtr(p->mBaseClass);
		}
		return false;
	}

	virtual void lockClass(PxI32 classId) = 0;

	virtual PxU32 getNbClasses() const = 0;
	virtual PxU32 getClasses(ClassDescription* outClasses, PxU32 requestCount, PxU32 startIndex = 0) const = 0;

	// Create a nested property.
	// This way you can have obj.p.x without explicity defining the class p.
	virtual Option<PropertyDescription> createProperty(PxI32 classId, String name, String semantic, PxI32 datatype,
	                                                   PropertyType::Enum propertyType = PropertyType::Scalar) = 0;
	Option<PropertyDescription> createProperty(NamespacedName clsId, String name, String semantic, NamespacedName dtype,
	                                           PropertyType::Enum propertyType = PropertyType::Scalar)
	{
		return createProperty(findClass(clsId)->mClassId, name, semantic, findClass(dtype)->mClassId, propertyType);
	}
	Option<PropertyDescription> createProperty(NamespacedName clsId, String name, NamespacedName dtype,
	                                           PropertyType::Enum propertyType = PropertyType::Scalar)
	{
		return createProperty(findClass(clsId)->mClassId, name, "", findClass(dtype)->mClassId, propertyType);
	}
	Option<PropertyDescription> createProperty(PxI32 clsId, String name, PxI32 dtype,
	                                           PropertyType::Enum propertyType = PropertyType::Scalar)
	{
		return createProperty(clsId, name, "", dtype, propertyType);
	}
	template <typename TDataType>
	Option<PropertyDescription> createProperty(PxI32 clsId, String name, String semantic = "",
	                                           PropertyType::Enum propertyType = PropertyType::Scalar)
	{
		return createProperty(clsId, name, semantic, getPvdNamespacedNameForType<TDataType>(), propertyType);
	}
	virtual Option<PropertyDescription> findProperty(const NamespacedName& cls, String prop) const = 0;
	virtual Option<PropertyDescription> findProperty(PxI32 clsId, String prop) const = 0;
	virtual Option<PropertyDescription> getProperty(PxI32 propId) const = 0;
	virtual void setNamedPropertyValues(DataRef<NamedValue> values, PxI32 propId) = 0;
	// for enumerations and flags.
	virtual DataRef<NamedValue> getNamedPropertyValues(PxI32 propId) const = 0;

	virtual PxU32 getNbProperties(PxI32 classId) const = 0;
	virtual PxU32 getProperties(PxI32 classId, PropertyDescription* outBuffer, PxU32 bufCount,
	                               PxU32 startIdx = 0) const = 0;

	// Does one cls id differ marshalling to another and if so return the functions to do it.
	virtual MarshalQueryResult checkMarshalling(PxI32 srcClsId, PxI32 dstClsId) const = 0;

	// messages and classes are stored in separate maps, so a property message can have the same name as a class.
	virtual Option<PropertyMessageDescription> createPropertyMessage(const NamespacedName& cls,
	                                                                 const NamespacedName& msgName,
	                                                                 DataRef<PropertyMessageArg> entries,
	                                                                 PxU32 messageSize) = 0;
	virtual Option<PropertyMessageDescription> findPropertyMessage(const NamespacedName& msgName) const = 0;
	virtual Option<PropertyMessageDescription> getPropertyMessage(PxI32 msgId) const = 0;

	virtual PxU32 getNbPropertyMessages() const = 0;
	virtual PxU32 getPropertyMessages(PropertyMessageDescription* msgBuf, PxU32 bufLen,
	                                     PxU32 startIdx = 0) const = 0;

	virtual StringTable& getStringTable() const = 0;

	virtual void write(PvdOutputStream& stream) const = 0;
	void save(PvdOutputStream& stream) const
	{
		write(stream);
	}

	virtual void addRef() = 0;
	virtual void release() = 0;

	static PxU32 getCurrentPvdObjectModelVersion();
	static PvdObjectModelMetaData& create();
	static PvdObjectModelMetaData& create(PvdInputStream& stream);
};
}
}
#endif
