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

#include "PxPvdUserRenderImpl.h"
#include "PxPvdInternalByteStreams.h"
#include "PxPvdBits.h"
#include <stdarg.h>

using namespace physx;
using namespace physx::pvdsdk;

namespace
{

template <typename TStreamType>
struct RenderWriter : public RenderSerializer
{
	TStreamType& mStream;
	RenderWriter(TStreamType& stream) : mStream(stream)
	{
	}
	template <typename TDataType>
	void write(const TDataType* val, PxU32 count)
	{
		PxU32 numBytes = count * sizeof(TDataType);
		mStream.write(reinterpret_cast<const uint8_t*>(val), numBytes);
	}
	template <typename TDataType>
	void write(const TDataType& val)
	{
		write(&val, 1);
	}

	template <typename TDataType>
	void writeRef(DataRef<TDataType>& val)
	{
		PxU32 amount = val.size();
		write(amount);
		if(amount)
			write(val.begin(), amount);
	}

	virtual void streamify(uint64_t& val)
	{
		write(val);
	}
	virtual void streamify(PxU32& val)
	{
		write(val);
	}
	virtual void streamify(float& val)
	{
		write(val);
	}
	virtual void streamify(uint8_t& val)
	{
		write(val);
	}
	virtual void streamify(DataRef<uint8_t>& val)
	{
		writeRef(val);
	}

	virtual void streamify(PxDebugText& val)
	{
		write(val.color);
		write(val.position);
		write(val.size);

		PxU32 amount = static_cast<PxU32>(strlen(val.string)) + 1;
		write(amount);
		if(amount)
			write(val.string, amount);
	}

	virtual void streamify(DataRef<PxDebugPoint>& val)
	{
		writeRef(val);
	}
	virtual void streamify(DataRef<PxDebugLine>& val)
	{
		writeRef(val);
	}
	virtual void streamify(DataRef<PxDebugTriangle>& val)
	{
		writeRef(val);
	}

	virtual PxU32 hasData()
	{
		return false;
	}
	virtual bool isGood()
	{
		return true;
	}

  private:
	RenderWriter& operator=(const RenderWriter&);
};

struct UserRenderer : public PvdUserRenderer
{
	ForwardingMemoryBuffer mBuffer;
	PxU32 mBufferCapacity;
	RendererEventClient* mClient;

	UserRenderer(PxU32 bufferFullAmount)
	: mBuffer("UserRenderBuffer"), mBufferCapacity(bufferFullAmount), mClient(NULL)
	{
	}
	virtual ~UserRenderer()
	{
	}
	virtual void release()
	{
		PVD_DELETE(this);
	}

	template <typename TEventType>
	void handleEvent(TEventType evt)
	{
		RenderWriter<ForwardingMemoryBuffer> _writer(mBuffer);
		RenderSerializer& writer(_writer);

		PvdUserRenderTypes::Enum evtType(getPvdRenderTypeFromType<TEventType>());
		writer.streamify(evtType);
		evt.serialize(writer);
		if(mBuffer.size() >= mBufferCapacity)
			flushRenderEvents();
	}
	virtual void setInstanceId(const void* iid)
	{
		handleEvent(SetInstanceIdRenderEvent(PVD_POINTER_TO_U64(iid)));
	}
	// Draw these points associated with this instance
	virtual void drawPoints(const PxDebugPoint* points, PxU32 count)
	{
		handleEvent(PointsRenderEvent(points, count));
	}
	// Draw these lines associated with this instance
	virtual void drawLines(const PxDebugLine* lines, PxU32 count)
	{
		handleEvent(LinesRenderEvent(lines, count));
	}
	// Draw these triangles associated with this instance
	virtual void drawTriangles(const PxDebugTriangle* triangles, PxU32 count)
	{
		handleEvent(TrianglesRenderEvent(triangles, count));
	}

	virtual void drawText(const PxDebugText& text)
	{
		handleEvent(TextRenderEvent(text));
	}

	virtual void drawRenderbuffer(const PxDebugPoint* pointData, PxU32 pointCount, const PxDebugLine* lineData,
	                              PxU32 lineCount, const PxDebugTriangle* triangleData, PxU32 triangleCount)
	{
		handleEvent(DebugRenderEvent(pointData, pointCount, lineData, lineCount, triangleData, triangleCount));
	}

	// Constraint visualization routines
	virtual void visualizeJointFrames(const PxTransform& parent, const PxTransform& child)
	{
		handleEvent(JointFramesRenderEvent(parent, child));
	}
	virtual void visualizeLinearLimit(const PxTransform& t0, const PxTransform& t1, float value, bool active)
	{
		handleEvent(LinearLimitRenderEvent(t0, t1, value, active));
	}
	virtual void visualizeAngularLimit(const PxTransform& t0, float lower, float upper, bool active)
	{
		handleEvent(AngularLimitRenderEvent(t0, lower, upper, active));
	}
	virtual void visualizeLimitCone(const PxTransform& t, float tanQSwingY, float tanQSwingZ, bool active)
	{
		handleEvent(LimitConeRenderEvent(t, tanQSwingY, tanQSwingZ, active));
	}
	virtual void visualizeDoubleCone(const PxTransform& t, float angle, bool active)
	{
		handleEvent(DoubleConeRenderEvent(t, angle, active));
	}
	// Clear the immedate buffer.
	virtual void flushRenderEvents()
	{
		if(mClient)
			mClient->handleBufferFlush(mBuffer.begin(), mBuffer.size());
		mBuffer.clear();
	}

	virtual void setClient(RendererEventClient* client)
	{
		mClient = client;
	}

  private:
	UserRenderer& operator=(const UserRenderer&);
};

template <bool swapBytes>
struct RenderReader : public RenderSerializer
{
	MemPvdInputStream mStream;
	ForwardingMemoryBuffer& mBuffer;

	RenderReader(ForwardingMemoryBuffer& buf) : mBuffer(buf)
	{
	}
	void setData(DataRef<const uint8_t> data)
	{
		mStream.setup(const_cast<uint8_t*>(data.begin()), const_cast<uint8_t*>(data.end()));
	}
	virtual void streamify(PxU32& val)
	{
		mStream >> val;
	}
	virtual void streamify(uint64_t& val)
	{
		mStream >> val;
	}
	virtual void streamify(float& val)
	{
		mStream >> val;
	}
	virtual void streamify(uint8_t& val)
	{
		mStream >> val;
	}
	template <typename TDataType>
	void readRef(DataRef<TDataType>& val)
	{
		PxU32 count;
		mStream >> count;
		PxU32 numBytes = sizeof(TDataType) * count;

		TDataType* dataPtr = reinterpret_cast<TDataType*>(mBuffer.growBuf(numBytes));
		mStream.read(reinterpret_cast<uint8_t*>(dataPtr), numBytes);
		val = DataRef<TDataType>(dataPtr, count);
	}

	virtual void streamify(DataRef<PxDebugPoint>& val)
	{
		readRef(val);
	}
	virtual void streamify(DataRef<PxDebugLine>& val)
	{
		readRef(val);
	}
	virtual void streamify(DataRef<PxDebugTriangle>& val)
	{
		readRef(val);
	}
	virtual void streamify(PxDebugText& val)
	{
		mStream >> val.color;
		mStream >> val.position;
		mStream >> val.size;

		PxU32 len = 0;
		mStream >> len;

		uint8_t* dataPtr = mBuffer.growBuf(len);
		mStream.read(dataPtr, len);
		val.string = reinterpret_cast<const char*>(dataPtr);
	}
	virtual void streamify(DataRef<uint8_t>& val)
	{
		readRef(val);
	}
	virtual bool isGood()
	{
		return mStream.isGood();
	}
	virtual PxU32 hasData()
	{
		return PxU32(mStream.size() > 0);
	}

  private:
	RenderReader& operator=(const RenderReader&);
};

template <>
struct RenderReader<true> : public RenderSerializer
{
	MemPvdInputStream mStream;
	ForwardingMemoryBuffer& mBuffer;
	RenderReader(ForwardingMemoryBuffer& buf) : mBuffer(buf)
	{
	}
	void setData(DataRef<const uint8_t> data)
	{
		mStream.setup(const_cast<uint8_t*>(data.begin()), const_cast<uint8_t*>(data.end()));
	}

	template <typename TDataType>
	void read(TDataType& val)
	{
		mStream >> val;
		swapBytes(val);
	}
	virtual void streamify(uint64_t& val)
	{
		read(val);
	}
	virtual void streamify(PxU32& val)
	{
		read(val);
	}
	virtual void streamify(float& val)
	{
		read(val);
	}
	virtual void streamify(uint8_t& val)
	{
		read(val);
	}
	template <typename TDataType>
	void readRef(DataRef<TDataType>& val)
	{
		PxU32 count;
		mStream >> count;
		swapBytes(count);
		PxU32 numBytes = sizeof(TDataType) * count;

		TDataType* dataPtr = reinterpret_cast<TDataType*>(mBuffer.growBuf(numBytes));
		PVD_FOREACH(idx, count)
		RenderSerializerMap<TDataType>().serialize(*this, dataPtr[idx]);
		val = DataRef<TDataType>(dataPtr, count);
	}

	virtual void streamify(DataRef<PxDebugPoint>& val)
	{
		readRef(val);
	}
	virtual void streamify(DataRef<PxDebugLine>& val)
	{
		readRef(val);
	}
	virtual void streamify(DataRef<PxDebugTriangle>& val)
	{
		readRef(val);
	}
	virtual void streamify(PxDebugText& val)
	{
		mStream >> val.color;
		mStream >> val.position;
		mStream >> val.size;

		PxU32 len = 0;
		mStream >> len;

		uint8_t* dataPtr = mBuffer.growBuf(len);
		mStream.read(dataPtr, len);
		val.string = reinterpret_cast<const char*>(dataPtr);
	}
	virtual void streamify(DataRef<uint8_t>& val)
	{
		readRef(val);
	}
	virtual bool isGood()
	{
		return mStream.isGood();
	}
	virtual PxU32 hasData()
	{
		return PxU32(mStream.size() > 0);
	}

  private:
	RenderReader& operator=(const RenderReader&);
};

}

PvdUserRenderer* PvdUserRenderer::create(PxU32 bufferSize)
{
	return PVD_NEW(UserRenderer)(bufferSize);
}

