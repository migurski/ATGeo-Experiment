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
