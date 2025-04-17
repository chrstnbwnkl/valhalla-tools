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
prints a list of Valhalla tile IDs that intersect with a given bounding box.
Usage:
  valhalla_get_tile_ids

  -h, --help              Print this help message.
  -b, --bounding-box arg  the bounding box to intersect with

```

## `valhalla_export_tiles`

```sh
exports edges and/or nodes into FlatGeoBuf files.

Usage:
  valhalla_export_tiles [OPTION...] TILEID

  -h, --help                    Print this help message.
  -j, --concurrency arg         Number of threads to use.
  -c, --config arg              Path to the json configuration file.
  -i, --inline-config arg       Inline json config.
  -o, --costing arg             Costing to use
  -e, --exclude-attributes arg  Attributes to exclude
  -a, --include-attributes arg  Attributes to include
  -f, --feature-type arg        Feature types to output (currently supports edges, nodes and restrictions yet to come)
  -d, --output-directory arg    Directory in which output files will be
                                written
```

You can use this tool together with `valhalla_get_tile_ids` by piping its output into this command:

```sh
valhalla_get_tile_ids -b 6.771468,50.761637,7.073568,51.051745 | valhalla_export_tiles -c valhalla.json -o auto -e edge.is_urban -e edge.use -d output  -j14
```

Thanks to the power of GDAL, this little program is pretty fast: on my 64GB RAM laptop with 16 logical cores, it spits out all edges in
Germany (~12GB) in 16 seconds and Europe (~70GB) in less than two minutes.

### Building from source

You need valhalla installed on your system. CMake will try to locate the lib and the headers using PkgConfig.

```sh
git submodules update --init --recursive
cmake -B build
cmake --build build -j$(nproc)
```
