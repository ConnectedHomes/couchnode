/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2012 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include "couchbase_impl.h"
#include "node_buffer.h"

using namespace Couchnode;

Persistent<Function> DefaultTranscoder::jsonParse;
Persistent<Function> DefaultTranscoder::jsonStringify;

enum Flags {
    // Node Flags - Formats
    NF_JSON = 0x00,
    NF_RAW = 0x02,
    NF_UTF8 = 0x04,
    NF_MASK = 0xFF,

    // Common Flags - Formats
    CF_JSON = 0x00,

    // Common Flags - Compressions
    CC_SNAPPY = 0x00
};

void DefaultTranscoder::Init()
{
    NanScope();

    Handle<Object> jMod = NanGetCurrentContext()->Global()->Get(
            NanNew<String>("JSON")).As<Object>();
    assert(!jMod.IsEmpty());

    NanAssignPersistent(jsonParse,
            jMod->Get(NanNew<String>("parse")).As<Function>());
    NanAssignPersistent(jsonStringify,
            jMod->Get(NanNew<String>("stringify")).As<Function>());
    assert(!jsonParse.IsEmpty());
    assert(!jsonStringify.IsEmpty());
}

Handle<Value> DefaultTranscoder::decode(const void *bytes,
        size_t nbytes, lcb_U32 flags)
{
    lcb_U32 format = flags & NF_MASK;

    if (format == NF_UTF8) {
        // UTF8 decodes into a String
        return NanNew<String>((char*)bytes, nbytes);
    } else if (format == NF_RAW) {
        // RAW decodes into a Buffer
        return NanNewBufferHandle((char*)bytes, nbytes);
    } else if (format == NF_JSON) {
        // JSON decodes using UTF8, then JSON.parse
        Handle<Value> utf8String = decode(bytes, nbytes, NF_UTF8);
        v8::TryCatch tryCatch;
        Local<Function> jsonParseLcl = NanNew(jsonParse);
        Handle<Value> ret = jsonParseLcl->Call(
                NanGetCurrentContext()->Global(), 1, &utf8String);
        if (!tryCatch.HasCaught()) {
            return ret;
        }

        // If there was an exception inside JSON.parse, we fall through
        //   to the default handling below and read it as RAW.
    }

    // Default to decoding as RAW
    return decode(bytes, nbytes, NF_RAW);
}

void DefaultTranscoder::encode(const void **bytes, lcb_SIZE *nbytes,
        lcb_U32 *flags, Handle<Value> value)
{
    if (value->IsString()) {
        encodeData = (char*)_NanRawString(value, Nan::UTF8, (size_t*)nbytes,
                NULL, 0, v8::String::NO_OPTIONS);
        *bytes = encodeData;
        *flags = NF_UTF8;
        return;
    } else if (node::Buffer::HasInstance(value)) {
        // This relies on the fact that value would have came from the
        //   function which invoked the operation, thus it's lifetime is
        //   implicitly going to outlive the command operation we create.
        *nbytes = node::Buffer::Length(value);
        *bytes = node::Buffer::Data(value);
        *flags = NF_RAW;
        return;
    } else {
        v8::TryCatch try_catch;
        Local<Function> jsonStringifyLcl = NanNew(jsonStringify);
        Handle<Value> ret = jsonStringifyLcl->Call(
                NanGetCurrentContext()->Global(), 1, &value);
        if (try_catch.HasCaught()) {
            // TODO: Better handling here...
            return;
        }

        encode(bytes, nbytes, flags, ret);
        *flags = NF_JSON;
        return;
    }
}
