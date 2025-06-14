#pragma once

#include <stdint.h>

// Encapsulates a typed array view into a blob of data.
#define SPAN_DEFINE(TYPE) typedef struct Span_ ## TYPE { \
     TYPE* data;        	     	         	         \
     uint32_t count;	         	     	        	 \
} Span_ ## TYPE

#define SPAN(TYPE, dataPtr, nElements) (Span_ ## TYPE){ \
        .data = dataPtr,                                \
        .count = nElements                              \
}

#define SPAN_BACK(span) (*(span.data + span.count - 1))
#define SPAN_BEGIN(span) span.data
#define SPAN_END(span) span.data + span.count
#define SPAN_AT(span, i) (*(span.data + i))