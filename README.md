# A Collection of useful tools for working with Valhalla data

A lot of little tools like these have been loosely flying around on my computer for a while now. I'll try my best to organize them in this
repo. Use at your own risk, the API might change any time. Contributions are fine, just please open an issue first.

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

Outputs one row per 5-minute bucket, each containing the index and the decoded speed separated by a comma.

## `valhalla_get_tile_ids`

```sh

```

## `valhalla_export_tiles`

```sh

```

### Building from source

You need valhalla installed on your system. CMake will try to locate the lib and the headers using PkgConfig.

```sh
git submodules update --init --recursive
cmake -B build
cmake --build build -j$(nproc)
```
