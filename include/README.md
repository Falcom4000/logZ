# logZ Header Files Structure

## File Organization

### Core Files

1. **Queue.h** - Lock-free queue implementation
   - Ring buffer for producer-consumer pattern
   - Thread-safe operations

2. **Decoder.h** - Decoder and formatter system
   - `DecoderFunc` - Function pointer type for decoders
   - `DecodedValueHandler` - Interface for handling decoded values
   - `LogFormatter` - Default formatter that outputs to string
   - `decode_single_arg_with_handler()` - Decode individual arguments
   - `decode_impl<Args...>()` - Compile-time generated decoder for each (Args...) combination
   - `get_decoder<Args...>()` - Get decoder function pointer

3. **Logger.h** - Main logger implementation
   - `LogLevel` - Log level enumeration
   - `LogMetadata` - Metadata structure (level, timestamp, decoder pointer, args size)
   - `Logger<MinLevel>` - Logger class template
   - `calculate_args_size()` - Calculate encoded size for arguments
   - `encode_single_arg()` - Encode individual arguments
   - `encode()` - Encode metadata and all arguments
   - `ThreadLocalLogger<MinLevel>` - Thread-local logger wrapper
   - Logging macros: `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_FATAL`

## Dependency Chain

```
Logger.h
  ├── Queue.h
  └── Decoder.h
```

## Usage

### Producer (Logging) Thread

```cpp
#include "Logger.h"

// Log with various argument types
LOG_INFO(42, "hello", 3.14);
```

### Consumer (Decoding) Thread

```cpp
#include "Logger.h"  // Includes Decoder.h

// Decode with custom handler
const std::byte* entry = ...; // Read from queue
const LogMetadata* meta = reinterpret_cast<const LogMetadata*>(entry);

// Method 1: Direct decoder call with LogFormatter
LogFormatter formatter;
const std::byte* args_ptr = entry + sizeof(LogMetadata);
meta->decoder(args_ptr, &formatter);
std::string formatted = formatter.get_formatted_string();
std::cout << formatted << std::endl;

// Method 2: Custom handler
class MyHandler : public DecodedValueHandler {
    void on_int(int value) override {
        // Custom handling
    }
    void on_string(const char* value) override {
        // Custom handling
    }
};

MyHandler handler;
args_ptr = entry + sizeof(LogMetadata);
meta->decoder(args_ptr, &handler);
```

## Key Design Points

1. **Compile-time Decoder Generation**
   - Each unique `(Args...)` combination generates a decoder at compile-time
   - Decoder function pointer stored in `LogMetadata`
   - No runtime template instantiation needed in consumer thread

2. **Type Safety**
   - Encoder and decoder use same type information
   - Compile-time type checking

3. **Extensibility**
   - Easy to add new types by extending `DecodedValueHandler`
   - Custom formatters can be created by inheriting from `DecodedValueHandler`

4. **Thread Safety**
   - Producer writes to queue
   - Consumer reads from queue
   - Decoder functions are stateless and thread-safe
