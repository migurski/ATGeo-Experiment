# C++ Port Plan

## Context

The Python Lambda (lambda.py) is working and smoke-tested. This document plans:

1. **Python unit tests** — `unittest` suite in `lambda.py` runnable via `python -m unittest lambda` without AWS.
2. **C++ port** — exact functional replica statically linked with minimal GDAL (GTiff + /vsis3/ only), deployable as a ZIP Lambda alongside the Python version.

---

## Part 1: Python unit tests

Run with: `python -m unittest lambda`

Tests exercise `decimal_precision()` directly and `handler()` with mocked GDAL reads (no real S3).

**Mock strategy**: `gdal.Open` returns a mock dataset whose `GetGeoTransform()` returns a plausible
global geotransform and whose `GetRasterBand(1).ReadRaster(...)` returns `struct.pack` bytes filled
with a fixed float value (1.5). Geotransforms used:
- hrsl-1.tif: `(-180.0, 0.1, 0, 90.0, 0, -0.1)`
- hrsl-2.tif: `(-180.0, 0.01, 0, 90.0, 0, -0.01)`
- hrsl-3.tif: `(-180.0, 0.001, 0, 90.0, 0, -0.001)`

**Test cases** (matching deploy.sh phase 8):

| Precision | lon | lat | Expected shape | Status |
|---|---|---|---|---|
| 0 digits | `-122` | `38` | 10×10 | 200 |
| 1 digit | `-122.3` | `37.8` | 10×10 | 200 |
| 2 digits | `-122.27` | `37.80` | 10×10 | 200 |
| 3 digits | `-122.271` | `37.804` | 1×1 | 200 |
| 4 digits | `-122.2713` | `37.8043` | — | 400 |
| missing params | — | — | — | 400 |

Also test `decimal_precision()` directly: `'38'→0`, `'37.8'→1`, `'37.80'→2`, `'-122.271'→3`.

**File modified**: `lambda.py` — append `import unittest` and `LambdaTests` class.

---

## Part 2: C++ port

### Architecture

- Language: C++17
- Lambda runtime: `awslabs/aws-lambda-cpp` (same as Clanker reference project)
- GDAL: compiled from source, minimal (GTiff + /vsis3/ only, static)
- AWS SDK: `aws-sdk-cpp` core only (Lambda JSON; GDAL handles /vsis3/ auth via credential chain)
- Deployment: ZIP-based `provided.al2023` custom runtime, ARM64
- Built entirely inside Docker (no local Mac tooling required)

### New files

| File | Purpose |
|---|---|
| `cpp/lambda.cpp` | C++ handler (exact functional replica of lambda.py) |
| `cpp/CMakeLists.txt` | Builds `bootstrap` linking GDAL + aws-lambda-runtime + aws-sdk-core |
| `cpp/Dockerfile` | Multi-stage: build deps → GDAL → aws-lambda-cpp → aws-sdk-cpp → app → zip |
| `cpp/build.sh` | Runs Docker build and extracts `lambda-cpp.zip` to project root |

### C++ handler design (`cpp/lambda.cpp`)

1. Parse Lambda event JSON (`Aws::Utils::Json::JsonValue`) — extract `queryStringParameters.lon/lat`
2. Replicate `decimal_precision()` — count chars after `.`
3. Precision → tif/step/half dispatch (same table as Python)
4. `GDALOpen(("/vsis3/" + bucket + "/" + tif).c_str(), GA_ReadOnly)` — uses AWS credential chain
5. `GetGeoTransform`, compute xoff/yoff/xsize/ysize with integer rounding
6. `GetRasterBand(1)->RasterIO(GF_Read, ...)` into a `float` buffer
7. Build JSON response: `ulx`, `uly`, `dx`, `dy`, `total`, `data` (nested JSON array)
8. Return `invocation_response::success(...)` or failure for errors

`DATA_BUCKET_NAME` read via `std::getenv("DATA_BUCKET_NAME")`.

### GDAL build flags (minimal)

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

### Docker layer order (cache-efficient)

1. Base: `public.ecr.aws/lambda/provided:al2023`
2. Build tools: cmake, gcc-c++, make, git, tar, openssl-devel, libcurl-devel, zlib-devel, sqlite-devel
3. Build GDAL from source (expensive — cached)
4. Build aws-lambda-cpp from source (expensive — cached)
5. Build aws-sdk-cpp core-only from source (expensive — cached)
6. `COPY cpp/lambda.cpp cpp/CMakeLists.txt /build/` — only this layer changes on code edits
7. Build app, zip bootstrap → `/lambda-cpp.zip`
8. Final scratch stage

### build.sh

```bash
docker build --target builder -t atgeo-cpp-builder cpp/
CONTAINER_ID=$(docker create atgeo-cpp-builder sh)
docker cp ${CONTAINER_ID}:/lambda-cpp.zip ./lambda-cpp.zip
docker rm ${CONTAINER_ID}
```

### Local testing (immediate goal)

Use the Lambda RIE bundled in the base image:

```bash
bash cpp/build.sh   # produces lambda-cpp.zip
# Run via RIE inside the Lambda base container:
docker run --rm -p 9000:8080 \
  -e DATA_BUCKET_NAME=atgeo-experiment-data-us-east-1-XXXX \
  -e AWS_DEFAULT_REGION=us-east-1 \
  -e AWS_ACCESS_KEY_ID=... \
  -e AWS_SECRET_ACCESS_KEY=... \
  public.ecr.aws/lambda/provided:al2023 bootstrap
# In another terminal:
curl -XPOST http://localhost:9000/2015-03-31/functions/function/invocations \
  -d '{"queryStringParameters":{"lon":"-122","lat":"38"}}'
```

### CloudFormation + deploy.sh integration (after local verification)

Add `LambdaFunctionCpp` resource to `application-template.yaml` (same role, same data bucket env var,
`Runtime: provided.al2023`, `PackageType: Zip`, `S3Key: lambda-cpp.zip`).

Add phases to `deploy.sh`:
- Build `lambda-cpp.zip` via `cpp/build.sh`
- Upload zip to bootstrap bucket
- Pass `LambdaCppPackageKey` parameter to application stack
- Smoke test the C++ function URL with the same 5 cases

---

## Verification

**Python tests:**
```bash
python -m unittest lambda
```
All 6+ test cases pass without network access.

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
