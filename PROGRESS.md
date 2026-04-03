# Progress

## CloudFormation deployment setup

Replaced the manual `publish.sh` AWS CLI script with a proper CloudFormation-based deployment pipeline.

- **`bootstrap-template.yaml`** — CloudFormation stack `atgeo-experiment-bootstrap` that creates a versioned, private S3 bucket (`atgeo-experiment-packages-{region}-{account}`) for storing Lambda deployment packages.
- **`application-template.yaml`** — CloudFormation stack `atgeo-experiment` that creates an IAM execution role, Lambda function, and public Lambda function URL.
- **`deploy.sh`** — Multi-phase deployment script covering bootstrap, build, upload, and application stack deploy.

## Data bucket and geotiff staging

Added a private S3 data bucket (`atgeo-experiment-data-{region}-{account}`) to the application stack, with the Lambda granted `s3:GetObject` on it and `DATA_BUCKET_NAME` passed as an environment variable.

Geotiffs (`hrsl-1.tif`, `hrsl-2.tif`) are staged to the bootstrap bucket under `geotiffs/` first, then S3-to-S3 synced into the data bucket — avoiding a full local re-upload on every deploy.

## Docker-based Lambda with GDAL via /vsis3/

Converted the Lambda from a zip-based Python function to a Docker image so GDAL can be included via `python3-gdal` from Ubuntu 24.04 apt.

- **`Dockerfile`** — Single-stage ARM64 Ubuntu image with `python3-gdal` and `awslambdaric`.
- **`bootstrap-template.yaml`** — Added ECR repository (`atgeo-experiment`) with lifecycle policy (keep last 10 images).
- **`application-template.yaml`** — Lambda now uses `PackageType: Image`, `Architectures: [arm64]`, and takes `ImageUri` parameter instead of `LambdaPackageKey`.
- **`deploy.sh`** — Added phases to build the ARM64 image with `docker buildx`, ECR login, and push; image tag derived from `git rev-parse --short HEAD`.
- **`lambda.py`** — Replaced PlanScore proxy with a GeoTIFF metadata reader using GDAL's `/vsis3/` virtual filesystem to read directly from S3 with no download step.

### Lambda function

Requests a GeoTIFF key by path; returns width, height, band count, and projection as JSON.

Live URL: `https://qndokzuxd5jnwc4wgixfhgei2m0kylnx.lambda-url.us-east-1.on.aws/`

Example: `curl "${url}hrsl-1.tif"` → `{"key": "hrsl-1.tif", "width": 3600, "height": 1800, "bands": 1, "projection": "GEOGCS[\"WGS 84\"...]"}`

## Population density query by lat/lon precision

Rewrote `lambda.py` to accept `?lon=&lat=` query parameters and return a 2D population density matrix from the appropriate GeoTIFF based on input decimal precision. The caller drills down by increasing precision until reaching a population density of interest.

| Precision | File | Window | Output |
|---|---|---|---|
| 0 digits | hrsl-1.tif | 1°×1° | 10×10 |
| 1 digit | hrsl-2.tif | 0.1°×0.1° | 10×10 |
| 2 digits | hrsl-3.tif | 0.01°×0.01° | 10×10 |
| 3 digits | hrsl-3.tif | 0.001°×0.001° | 1×1 |
| 4+ digits | — | HTTP 400 | — |

Pixels are read directly from the GeoTIFF via `/vsis3/` using `ReadRaster` + `struct.unpack` (no numpy, no download, no warp). Response includes `ulx`, `uly`, `dx`, `dy` envelope keys plus a `data` matrix with `null` where the source has NaN.

Image tag changed from git commit hash to `latest`, with the post-push ECR digest used as the CloudFormation `ImageUri` parameter so CFN always detects image changes without extra CLI calls.

Smoke tests added as phase 8 of `deploy.sh`, checking shape and HTTP status for all four precision levels plus the 400 case.

## Python unit tests

Added `LambdaTests(unittest.TestCase)` to `lambda.py`, runnable via `python -m unittest lambda` with no network access.

- 11 test cases covering `decimal_precision()`, handler shape (10×10 at 0/1/2 digits, 1×1 at 3 digits), HTTP 400 error paths, and presence of envelope keys (`ulx`, `uly`, `dx`, `dy`, `total`, `data`).
- Uses `unittest.mock.patch` to stub `gdal.Open` with per-file GeoTransforms matching each HRSL resolution.

## C++ port with static GDAL

Added an exact C++ replica of the Lambda function in `cpp/`, buildable via Docker with no local Mac tooling. Produces `lambda-cpp.zip` (17 MB) deployable as a `provided.al2023` ZIP Lambda alongside the Python Docker version.

- **`cpp/lambda.cpp`** — C++17 handler: parses `queryStringParameters`, replicates `decimal_precision()` and the precision→tif/step/half dispatch, calls `GDALOpen("/vsis3/...")`, computes pixel offsets from `GetGeoTransform`, reads via `RasterIO` into a `float` buffer, builds the JSON response manually.
- **`cpp/CMakeLists.txt`** — Links `aws-lambda-runtime`, `aws-sdk-cpp` (core only), and GDAL via `pkg_check_modules` (which carries all transitive deps from the installed `.pc` file).
- **`cpp/Dockerfile`** — Multi-stage ARM64 build on `provided:al2023`: PROJ 9.5.1, GDAL 3.10.2 (GTiff + `/vsis3/` only, static), `aws-lambda-cpp`, `aws-sdk-cpp` core, then app; exports `bootstrap.zip` renamed to `lambda-cpp.zip`.
- **`cpp/build.sh`** — Builds the Docker image and extracts `lambda-cpp.zip` to the project root.
- **`PORT.md`** — Reference document for the C++ port architecture and deployment plan.

### Key build lessons

- GDAL requires PROJ as a mandatory dependency (no cmake flag to disable it); PROJ must be built from source before GDAL.
- `GDAL::GDAL` cmake imported target does not reliably propagate all transitive deps (sqlite3, pcre2, etc.) in a static-link context. Using `pkg_check_modules(GDAL gdal)` and `${GDAL_LINK_LIBRARIES}` instead gives the complete link flags from GDAL's installed `.pc` file.
- `aws_lambda_package_target` names the zip after the binary (`bootstrap.zip`), not the CMake project name.
- The zip contains a `bootstrap` shell wrapper that sets `LD_LIBRARY_PATH` and execs `bin/bootstrap`; for local testing the full zip must be extracted to `$LAMBDA_TASK_ROOT` (`/var/task`).

### Local smoke test

All 6 cases verified locally via Lambda RIE (`public.ecr.aws/lambda/provided:al2023`):

```
200 10×10  lon=-122    lat=38
200 10×10  lon=-122.3  lat=37.8
200 10×10  lon=-122.27 lat=37.80
200  1×1   lon=-122.271 lat=37.804
400        lon=-122.2713 lat=37.8043
400        (missing params)
```
