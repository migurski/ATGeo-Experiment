# C++ Port

## Architecture

- Language: C++17
- Lambda runtime: `awslabs/aws-lambda-cpp`
- GDAL: compiled from source, minimal (GTiff + /vsis3/ only, static)
- AWS SDK: `aws-sdk-cpp` core only (Lambda JSON; GDAL handles /vsis3/ auth via credential chain)
- Deployment: ZIP-based `provided.al2023` custom runtime, ARM64
- Built entirely inside Docker (no local Mac tooling required)

## Files

| File | Purpose |
|---|---|
| `cpp/lambda.cpp` | C++ handler (exact functional replica of lambda.py) |
| `cpp/CMakeLists.txt` | Builds `bootstrap` linking GDAL + aws-lambda-runtime + aws-sdk-core |
| `cpp/Dockerfile` | Multi-stage: build deps → GDAL → aws-lambda-cpp → aws-sdk-cpp → app → zip |
| `cpp/build.sh` | Runs Docker build and extracts `lambda-cpp.zip` to project root |

## C++ handler design (`cpp/lambda.cpp`)

`DATA_BUCKET_NAME` read via `std::getenv("DATA_BUCKET_NAME")` at the top of `handler()`,
before routing. `rawPath` (or `path`) determines which sub-handler is called.

### Decimal-degrees handler (`/` or default)

1. Parse Lambda event JSON (`Aws::Utils::Json::JsonValue`) — extract `queryStringParameters.lon/lat`
2. Replicate `decimal_precision()` — count chars after `.`
3. Precision → tif/step/half dispatch — TIF names: `degree-1digit.tif`, `degree-2digit.tif`, `degree-3digit.tif`
4. `GDALOpen(("/vsis3/" + bucket + "/" + tif).c_str(), GA_ReadOnly)` — uses AWS credential chain
5. `GetGeoTransform`, compute xoff/yoff/xsize/ysize with integer rounding
6. `GetRasterBand(1)->RasterIO(GF_Read, ...)` into a `float` buffer
7. Build JSON response: `ulx`, `uly`, `dx`, `dy`, `total`, `data` (nested JSON array)
8. Return `invocation_response::success(...)` or failure for errors

### Quadkey handler (`/dgg?quadkey=`) — `quadkey_handler()`

Port of Python `quadkey_handler` and `_quadkey_tile_xy()`:

1. `quadkey_tile_xy(key)` — decompose each base-4 digit to 2 bits (high bit → ybin, low
   bit → xbin), parse xbin/ybin as binary integers to get tile column/row
2. Validate `quadkey` param: present, 1–18 chars, all digits 0–3
3. Compute parent upper-left in EPSG:3857: `ulx = -MERCATOR_HALF + xtile * pixel_size`,
   `uly = MERCATOR_HALF - ytile * pixel_size`
4. For len < 18: loop depths `len+1` to `min(len+3, 18)`; for each depth enumerate all
   `4^suffix_len` children by counting `i` from 0 to `4^N-1` and converting to base-4
   zero-padded suffix; call `quadkey_tile_xy(child)`, `RasterIO` 1×1 each
5. For len == 18: open `quadkey-18char.tif`, read single pixel
6. `dx`/`dy` from finest child pixel size in EPSG:3857 meters
7. Build JSON: `{"quadkey":…,"ulx":…,"uly":…,"dx":…,"dy":…,"total":…,"sub-areas":{…}}`

### Geohash handler (`/dgg`) — `dgg_handler()`

Port of Python `dgg_handler` and `geohash.geohash2lonlats()`:

1. `geohash2lonlats(gh)` — decode each char to 5 bits via `GEOHASH_ALPHABET.find()`, split
   interleaved bits into xbin/ybin, parse as binary fractions, scale to lon/lat
2. Validate `geohash` param: present, 1–7 chars, all in `GEOHASH_ALPHABET`
3. Compute parent bbox via `geohash2lonlats(gh)` and child cell dimensions from first child
   (or same cell at max depth) — used for `ulx`, `uly`, `dx`, `dy` in response
4. For 1–6 chars: open `geohash-(N+1)char.tif`, loop all 32 `GEOHASH_ALPHABET` children,
   call `geohash2lonlats(child)`, compute center pixel offset, `RasterIO` 1×1 each
5. For 7 chars: open `geohash-7char.tif`, read single pixel for the cell itself
6. Build JSON: `{"geohash":…,"ulx":…,"uly":…,"dx":…,"dy":…,"total":…,"sub-areas":{…}}` via `ostringstream`

## GDAL build flags (minimal)

```cmake
cmake /src/gdal \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=OFF \
  -DGDAL_BUILD_OPTIONAL_DRIVERS=OFF \
  -DGDAL_ENABLE_DRIVER_GTIFF=ON \
  -DGDAL_USE_TIFF_INTERNAL=ON \
  -DGDAL_USE_GEOTIFF_INTERNAL=ON \
  -DGDAL_USE_CURL=ON \
  -DBUILD_PYTHON_BINDINGS=OFF \
  -DBUILD_APPS=OFF \
  -DENABLE_TESTING=OFF
```

GDAL fetched from `https://github.com/OSGeo/gdal/releases` at build time for reproducibility.

## Docker layer order (cache-efficient)

1. Base: `public.ecr.aws/lambda/provided:al2023`
2. Build tools: cmake, gcc-c++, make, git, tar, openssl-devel, libcurl-devel, zlib-devel, sqlite-devel
3. Build GDAL from source (expensive — cached)
4. Build aws-lambda-cpp from source (expensive — cached)
5. Build aws-sdk-cpp core-only from source (expensive — cached)
6. `COPY lambda.cpp CMakeLists.txt /build/` — only this layer changes on code edits
7. Build app, zip bootstrap → `/lambda-cpp.zip`
8. Final scratch stage

## build.sh

```bash
docker build --target builder -t atgeo-cpp-builder cpp/
CONTAINER_ID=$(docker create atgeo-cpp-builder sh)
docker cp ${CONTAINER_ID}:/lambda-cpp.zip ./lambda-cpp.zip
docker rm ${CONTAINER_ID}
```

## Local testing

`build.sh` produces both the Lambda zip and the `atgeo-cpp-cli` Docker image:

```bash
bash cpp/build.sh
```

Run the CLI directly against a local GeoTIFF directory:

```bash
docker run --rm \
  -v "$(pwd)/geotiffs:/geotiffs:ro" \
  -e GEOTIFF_DIR=/geotiffs \
  atgeo-cpp-cli --lonlat -122.3 37.8

docker run --rm \
  -v "$(pwd)/geotiffs:/geotiffs:ro" \
  -e GEOTIFF_DIR=/geotiffs \
  atgeo-cpp-cli --geohash 9q9p1d

docker run --rm \
  -v "$(pwd)/geotiffs:/geotiffs:ro" \
  -e GEOTIFF_DIR=/geotiffs \
  atgeo-cpp-cli --quadkey 0230102
```

## CloudFormation + deploy.sh integration

All of this is already in place. Notes:

- `LambdaFunctionCpp` in `application-template.yaml` uses `provided.al2023`, `PackageType: Zip`
- The `CppCachePolicy` query string whitelist includes `lon`, `lat`, `geohash`, and `quadkey` —
  all must be listed or CloudFront strips them before forwarding to the Lambda
- `deploy.sh` smoke tests hit the raw CloudFront domain (`CppCloudFrontDomain` output) rather
  than the custom domain alias, which had unreliable DNS resolution

## Performance characteristics

Measured 2026-04-06 after 30-minute idle (true cold start), 3 smoke-test runs back-to-back.
Both functions: ARM64, `provided.al2023` (C++) / container image (Python).

**Memory allocation: 256 MB** (reduced from 512 MB — peak usage is 105–124 MB Python,
50–75 MB C++; cold start and warm execution are unaffected by the reduction).

### GDAL environment variables (set on both functions)

```
GDAL_DISABLE_READDIR_ON_OPEN=EMPTY_DIR
CPL_VSIL_CURL_ALLOWED_EXTENSIONS=.tif
```

Without these, GDAL probes for `.aux.xml`, `.aux`, `.AUX` etc. before each TIFF open, producing
5 × HTTP 403 round trips per request. With them: no probing, no 403s.
Effect: Python cold start dropped from ~1300 ms to ~400 ms; warm GDAL lookups improved 30–40%.

### Cold start

| Function | Init Duration | First request | Total billed |
|---|---|---|---|
| Python | ~360–400 ms | ~190–350 ms | ~600–720 ms |
| C++ | ~280 ms | ~250–380 ms | ~530–660 ms |

C++ cold start is ~1.4× faster than Python. (Before GDAL env vars: Python was ~1300 ms, a 4.5× gap.)
Cold start is not memory-bound: reducing from 512 MB to 256 MB had no effect on init times.

### Warm execution

**The dominant factor is GDAL block cache warmup, not language.**

- Run 1 (first requests after cold start): GDAL fetches TIFF blocks from S3 — 55–490 ms per request
- Run 2+: same TIFFs served from in-memory block cache — **2–53 ms per request**

Representative timings at 256 MB:

| Request | Python run 1 | Python run 2+ | C++ run 1 | C++ run 2+ |
|---|---|---|---|---|
| geohash 4-char | 55 ms | 3–12 ms | 115 ms | 2–3 ms |
| geohash 7-char | 185 ms | 3–17 ms | 335 ms | 2–3 ms |
| 1-char geohash (large) | 57 ms | 3 ms | 374–490 ms | 2–53 ms |
| quadkey 7-char | 47 ms | 11–43 ms | 453 ms | 41–52 ms |
| trivial / 400 error | 1–2 ms | 1–2 ms | 1–2 ms | 1–3 ms |

C++ run-1 warm requests are notably slower than Python (up to 490 ms vs ~185 ms), likely due to
different GDAL block-fetching patterns. Both converge to low single-digit ms by run 2 for most
request types; large quadkey/geohash lookups may take a second warm pass to fully cache.

Memory usage: C++ 50–75 MB, Python 105–124 MB (both at 256 MB allocation).

### Summary

256 MB is the right allocation. Cold start and warm execution are identical to 512 MB, at half
the GB-second cost. C++ saves ~70–120 ms on cold start init and uses ~half the memory. For a
warm container under sustained load, both functions are effectively free (<10 ms for most
requests) once the GDAL block cache is populated after the first pass through each TIFF.

## Verification

**C++ local:**
```bash
bash cpp/build.sh
unzip -l lambda-cpp.zip   # verify bootstrap present
# RIE test as above
```

**C++ Lambda (after deploy):**
```bash
bash deploy.sh   # smoke tests cover both Python and C++ functions
```
