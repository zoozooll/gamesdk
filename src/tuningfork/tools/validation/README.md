# Tuning Fork Validation tool

Current tool validates proto and settings files in APK.

## tuningfork_settings

Apk must contain *assets/tuningfork/tuningfork_settings.bin* file with
serialized data for `Settings` proto message:

```proto
message Settings {
  message Histogram {
    optional int32 instrument_key = 1;
    optional float bucket_min = 2;
    optional float bucket_max = 3;
    optional int32 n_buckets = 4;
  }
  message AggregationStrategy {
    enum Submission {
      TIME_BASED = 1;
      TICK_BASED = 2;
    }
    optional Submission method = 1;
    optional int32 intervalms_or_count = 2;
    optional int32 max_instrumentation_keys = 3;
    repeated int32 annotation_enum_size = 4;
  }
  optional AggregationStrategy aggregation_strategy = 1;
  repeated Histogram histograms = 2;
}
```

### Settings validation

* At least one histogram
* `max_instrumentation_keys` must be between 1 and 256
* `annotation_enum_size` must match `Annotation` message (see below)

### Example
Example of data before serialization:

```textproto
aggregation_strategy:
{
  method: TIME_BASED,
  intervalms_or_count: 600000,
  max_instrumentation_keys: 2,
  annotation_enum_size: [2, 3]
}
histograms:
[
  {
    instrument_key: 0,
    bucket_min: 28,
    bucket_max: 32,
    n_buckets: 70
  },
  {
    instrument_key: 1,
    bucket_min: 28,
    bucket_max: 32,
    n_buckets: 70
  }
]
```

## dev_tuningfork.proto

Apk must contain *assets/tuningfork/dev_tuningfork.proto* file with `Annotation`
and `FidelityParams` proto message.

### Validation

Both messages (`Annotation` and `FidelityParams`) must follow these rules
* No oneofs
* No Nested types 
* No extensions
* Enums must not contains fields with 0 index

Additional limitation for `Annotation` message only
* Only `ENUM` types
* Size of enums must match a`nnotation_enum_size` field in settings.

Additional limitation for `FidelityParams` messsage only
* Only `ENUM`, `FLOAT` and `INT32` types

### Example

Valid .proto file:

```proto
syntax = "proto2";

package com.google.tuningfork;

enum LoadingState {
  LOADING = 1;
  NOT_LOADING = 2;
}

enum Level {
  Level_1 = 1;
  Level_2 = 2;
  Level_3 = 3;
}

message Annotation {
  optional LoadingState loading_state = 1;
  optional Level level = 2;
}

enum QualitySettings {
  FASTEST = 1;
  FAST = 2;
  SIMPLE = 3;
  GOOD = 4;
  BEAUTIFUL = 5;
  FANTASTIC = 6;
}

message FidelityParams {
  optional QualitySettings quality_settings = 1;
  optional int32 lod_level = 2;
  optional float distance = 3;
}
```

## dev_tuningfork_fidelityparams

Apk must contain at least one file in assets/tuningfork folder with pattern 
*dev_tuningfork_fidelityparams_.{1,15}.bin*. Each file contains serialized 
parameters for `FidelityParams` proto message from *dev_tuningfork.proto* file.


