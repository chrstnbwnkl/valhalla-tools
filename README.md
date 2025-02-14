# A Collection of useful tools and functions for working with Valhalla data

### CLI tools

## `valhalla_remove_predicted_traffic`

```sh
removes predicted traffic from valhalla tiles.

Usage:
  valhalla_remove_predicted_traffic

  -h, --help               Print this help message.
  -j, --concurrency arg    Number of threads to use.
  -c, --config arg         Path to the json configuration file.
  -i, --inline-config arg  Inline json config.

```

## `valhalla_decode_buckets`

```sh
valhalla_decode_buckets

valhalla_decode_buckets is a program that decodes
encoded speed buckets.

Usage:
  valhalla_decode_buckets ENCODED The encoded string to process

  -h, --help  Print this help message.
```

### Building from source

```sh
cmake -B build
cmake --build build -j$(nproc)
```
