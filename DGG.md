# /dgg Geohash Endpoint — Progress Notes

## What's done

### API (`openapi.yaml`)
- Added `GET /dgg` path with `geohash` query parameter (string, 1–7 chars)
- Added `GeohashResponse` schema with `geohash`, `ulx`, `uly`, `dx`, `dy`, `total`, `sub-areas`
- Each sub-area entry: `{"link": "/dgg?geohash=<child>", "count": <float>}`
- Error responses: 400 for missing/invalid/too-long geohash

### Infrastructure (`application-template.yaml`)
- Added `LambdaFunctionUrl` and `LambdaFunctionUrlPermission` for the Python Lambda
- Added `FunctionUrl` output

### Docker image (`Dockerfile`)
- Added `geohash.py` to the `COPY` step alongside `lambda.py`

### Code (`lambda.py`, `geohash.py`)
- Added `import geohash` to `lambda.py`
- Added `dgg_handler()`:
  - Validates `geohash` param: present, 1–7 chars, all chars in `ALPHABET`
  - For 1–6 char input: reads `geohash-(N+1)char.tif`, makes 32 single-pixel reads
    (one per child geohash), returns 32 sub-areas
  - For 7-char input: reads `geohash-7char.tif`, returns single pixel for the cell
    itself (same pattern as decimal-degree precision==3 returning 1×1)
  - Response includes `ulx`, `uly` (parent cell upper-left corner) and `dx`, `dy`
    (child cell dimensions) derived from `geohash2lonlats()` — no extra TIFF reads
- Added routing in `handler()` via `rawPath`/`path` to dispatch `/dgg` calls
- Added 10 unit tests in `DggHandlerTests`

### Deploy (`deploy.sh`)
- Retrieves `FunctionUrl` from CloudFormation outputs
- Prints Python Lambda URL alongside C++ URLs
- Added `smoke_test_dgg()` function testing: 1-char, 3-char, 7-char geohashes,
  8-char (expect 400), missing param (expect 400)
- Renamed `smoke_test` → `smoke_test_dd` for clarity

### C++ port (`cpp/lambda.cpp`)
- Ported `geohash2lonlats()` and `dgg_handler()` to C++17
- Fixed pre-existing TIFF name bug: `hrsl-{1,2,3}.tif` → `degree-{1,2,3}digit.tif`
- Added `ulx`, `uly`, `dx`, `dy` to C++ `/dgg` response

### Infrastructure (`application-template.yaml`)
- Added `geohash` to the CloudFront `CppCachePolicy` query string whitelist
  (without this, the `geohash` param was stripped before reaching the C++ Lambda)

### Docs (`README.md`)
- Added `/dgg` section with real drill-down from `9` → `9q9p1dh` for 14th & Broadway Oakland

## Live test results (2026-04-05)

CloudFront URL: `https://d1dksi21h1fuhd.cloudfront.net/` (C++)
Python Lambda URL: `https://qj2fkboi44gbybzl6fzaa5ssee0bgmio.lambda-url.us-east-1.on.aws/`

```
/dgg?geohash=9       → 32 sub-areas, total 266,698,940.7, ulx -135.0, uly 45.0
/dgg?geohash=9q9p1d  → 32 sub-areas, total 2,936.7, ulx -122.278, uly 37.809
/dgg?geohash=9q9p1dh → 1 sub-area (single pixel), total 6.2 (14th & Broadway Oakland)
/dgg?geohash=u4pruydq → HTTP 400
/dgg                  → HTTP 400
```

Both Python and C++ return matching results. C++ smoke tests pass via CloudFront domain
(DNS for the custom domain alias was unreliable; `deploy.sh` now uses CloudFront URL directly).
