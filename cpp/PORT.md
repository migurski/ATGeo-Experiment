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

### Geohash handler (`/dgg`) — `dgg_handler()`

Port of Python `dgg_handler` and `geohash.geohash2lonlats()`:

1. `geohash2lonlats(gh)` — decode each char to 5 bits via `GEOHASH_ALPHABET.find()`, split
   interleaved bits into xbin/ybin, parse as binary fractions, scale to lon/lat
2. Validate `geohash` param: present, 1–7 chars, all in `GEOHASH_ALPHABET`
3. For 1–6 chars: open `geohash-(N+1)char.tif`, loop all 32 `GEOHASH_ALPHABET` children,
   call `geohash2lonlats(child)`, compute center pixel offset, `RasterIO` 1×1 each
4. For 7 chars: open `geohash-7char.tif`, read single pixel for the cell itself
5. Build JSON: `{"geohash":…,"total":…,"sub-areas":{…}}` via `ostringstream`

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

Use the Lambda RIE bundled in the base image:

```bash
bash cpp/build.sh   # produces lambda-cpp.zip
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

## CloudFormation + deploy.sh integration (after local verification)

Add `LambdaFunctionCpp` resource to `application-template.yaml` (same role, same data bucket env var,
`Runtime: provided.al2023`, `PackageType: Zip`, `S3Key: lambda-cpp.zip`).

Add phases to `deploy.sh`:
- Build `lambda-cpp.zip` via `cpp/build.sh`
- Upload zip to bootstrap bucket
- Pass `LambdaCppPackageKey` parameter to application stack
- Smoke test the C++ function URL with the same 5 cases

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
