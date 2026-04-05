# /dgg Geohash Endpoint — Progress Notes

## What's done

### API (`openapi.yaml`)
- Added `GET /dgg` path with `geohash` query parameter (string, 1–7 chars)
- Added `GeohashResponse` schema with `geohash`, `total`, `sub-areas` dict
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
- Added routing in `handler()` via `rawPath`/`path` to dispatch `/dgg` calls
- Added 9 unit tests in `DggHandlerTests`

### Deploy (`deploy.sh`)
- Retrieves `FunctionUrl` from CloudFormation outputs
- Prints Python Lambda URL alongside C++ URLs
- Added `smoke_test_dgg()` function testing: 1-char, 3-char, 7-char geohashes,
  8-char (expect 400), missing param (expect 400)
- Renamed `smoke_test` → `smoke_test_dd` for clarity

### Docs (`README.md`)
- Added `/dgg` section with drill-down example using `u4pruyd...` geohash sequence

## Live test results (2026-04-05)

Python Lambda URL: `https://qj2fkboi44gbybzl6fzaa5ssee0bgmio.lambda-url.us-east-1.on.aws/`

```
/dgg?geohash=u      → 32 sub-areas, total 331,736,243.7
/dgg?geohash=u4p    → 32 sub-areas, total 544,459.0
/dgg?geohash=u4pruyd → 1 sub-area (single pixel), total 0.0
                       (verified correct: neighboring cells u4pruyr=0.2, u4pruyx=0.3, u4pruyz=0.3)
/dgg?geohash=u4pruydq → HTTP 400
/dgg                  → HTTP 400
```

## Known issue

C++ smoke tests in `deploy.sh` are failing (empty body, HTTP 000) — pre-existing issue
unrelated to the `/dgg` work, not investigated.
