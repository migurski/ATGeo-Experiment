#!/bin/bash -e

app_stack_name="${1:-atgeo-experiment}"

# Retrieve stack outputs
function_url=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='FunctionUrl'].OutputValue" \
    --output text)

function_cpp_url=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='FunctionCppUrl'].OutputValue" \
    --output text)

echo "Function URL (Python): ${function_url}"
echo "Function URL (C++):    ${function_cpp_url}"

# Usage: check_dd_json <url> <expected_rows> <expected_cols> <expected_total> <tolerance>
check_dd_json() {
    local url="$1"
    local exp_rows="$2"
    local exp_cols="$3"
    local exp_total="$4"
    local tolerance="$5"

    curl -sf "${url}" | jq --argjson exp_rows "${exp_rows}" \
        --argjson exp_cols "${exp_cols}" \
        --argjson exp_total "${exp_total}" \
        --argjson tol "${tolerance}" '
        "\(.ulx) \(.uly) \(.dx) \(.dy) \(.data | length) x \(.data[0] | length) total: \(.total)",
        if (.data | length) != $exp_rows then error("expected \($exp_rows) rows, got \(.data | length)") else empty end,
        if (.data[0] | length) != $exp_cols then error("expected \($exp_cols) cols, got \(.data[0] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
    '
}

# Usage: check_dgg_json <url> <expected_total> <tolerance>
check_dgg_json() {
    local url="$1"
    local exp_total="$2"
    local tolerance="$3"

    curl -sf "${url}" | jq --argjson exp_total "${exp_total}" \
        --argjson tol "${tolerance}" '
        "\(.geohash) total: \(.total) sub-areas: \(.["sub-areas"] | length)",
        if (.["sub-areas"] | length) != 32 then error("expected 32 sub-areas, got \(.["sub-areas"] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
    '
}

smoke_test_dd() {
    local label="$1"
    local url="$2"

    echo "--- ${label} 0 digits (expect 10x10, total~4535260) ---"
    check_dd_json "${url}?lon=-122&lat=38" 10 10 4535260 1000

    echo "--- ${label} 1 digit (expect 10x10, total~164311) ---"
    check_dd_json "${url}?lon=-122.3&lat=37.8" 10 10 164311 1000

    echo "--- ${label} 2 digits (expect 10x10, total~7198.8) ---"
    check_dd_json "${url}?lon=-122.27&lat=37.80" 10 10 7198.8 100

    echo "--- ${label} 3 digits (expect 1x1) ---"
    check_dd_json "${url}?lon=-122.271&lat=37.804" 1 1 5.3 10

    echo "--- ${label} 4 digits (expect HTTP 400) ---"
    status=$(curl -s -o /dev/null -w "%{http_code}" "${url}?lon=-122.2713&lat=37.8043")
    echo "${status}"
    test "${status}" = "400"
    echo 'OK'
}

smoke_test_dgg() {
    local label="$1"
    local url="$2"

    echo "--- ${label} /dgg 1-char geohash (expect 32 sub-areas, total~266698940.7) ---"
    check_dgg_json "${url}/dgg?geohash=9" 266698940.7 10000

    echo "--- ${label} /dgg 3-char geohash (expect 32 sub-areas, total~5762930) ---"
    check_dgg_json "${url}/dgg?geohash=9q9" 5762930 10000

    echo "--- ${label} /dgg 7-char geohash (expect 32 sub-areas, total~2936.7) ---"
    check_dgg_json "${url}/dgg?geohash=9q9p1d" 2936.7 100

    echo "--- ${label} /dgg 8-char geohash (expect HTTP 400) ---"
    status=$(curl -s -o /dev/null -w "%{http_code}" "${url}/dgg?geohash=u4pruydq")
    echo "${status}"
    test "${status}" = "400"
    echo 'OK'

    echo "--- ${label} /dgg missing geohash (expect HTTP 400) ---"
    status=$(curl -s -o /dev/null -w "%{http_code}" "${url}/dgg")
    echo "${status}"
    test "${status}" = "400"
    echo 'OK'
}

# Usage: check_quadkey_json <url> <expected_count> <expected_total> <tolerance>
check_quadkey_json() {
    local url="$1"
    local exp_count="$2"
    local exp_total="$3"
    local tolerance="$4"

    curl -sf "${url}" | jq --argjson exp_count "${exp_count}" \
        --argjson exp_total "${exp_total}" \
        --argjson tol "${tolerance}" '
        "\(.quadkey) total: \(.total) sub-areas: \(.["sub-areas"] | length)",
        if (.["sub-areas"] | length) != $exp_count then error("expected \($exp_count) sub-areas, got \(.["sub-areas"] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
    '
}

smoke_test_quadkey() {
    local label="$1"
    local url="$2"

    echo "--- ${label} /dgg 7-char quadkey (expect 84 sub-areas) ---"
    check_quadkey_json "${url}/dgg?quadkey=0230102" 84 0 9999999

    echo "--- ${label} /dgg 16-char quadkey (expect 20 sub-areas, depths 17+18) ---"
    check_quadkey_json "${url}/dgg?quadkey=0230102122203301" 20 0 9999999

    echo "--- ${label} /dgg 18-char quadkey (expect 1 sub-area) ---"
    check_quadkey_json "${url}/dgg?quadkey=023010212220330102" 1 0 9999999

    echo "--- ${label} /dgg 19-char quadkey (expect HTTP 400) ---"
    status=$(curl -s -o /dev/null -w "%{http_code}" "${url}/dgg?quadkey=0230102122203301023")
    echo "${status}"
    test "${status}" = "400"
    echo 'OK'

    echo "--- ${label} /dgg missing quadkey (expect HTTP 400) ---"
    status=$(curl -s -o /dev/null -w "%{http_code}" "${url}/dgg?quadkey=")
    echo "${status}"
    test "${status}" = "400"
    echo 'OK'
}

smoke_test_local() {
    echo "--- local Python 1 digit (expect 10x10, total~164311) ---"
    GEOTIFF_DIR=geotiffs python lambda.py --lonlat -122.3 37.8 \
        | jq --argjson exp_rows 10 --argjson exp_cols 10 \
             --argjson exp_total 164311 --argjson tol 1000 '
        "\(.ulx) \(.uly) \(.dx) \(.dy) \(.data | length) x \(.data[0] | length) total: \(.total)",
        if (.data | length) != $exp_rows then error("expected \($exp_rows) rows, got \(.data | length)") else empty end,
        if (.data[0] | length) != $exp_cols then error("expected \($exp_cols) cols, got \(.data[0] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
        '

    echo "--- local Python /dgg 6-char geohash (expect 32 sub-areas, total~2936.7) ---"
    GEOTIFF_DIR=geotiffs python lambda.py --geohash 9q9p1d \
        | jq --argjson exp_total 2936.7 --argjson tol 100 '
        "\(.geohash) total: \(.total) sub-areas: \(.["sub-areas"] | length)",
        if (.["sub-areas"] | length) != 32 then error("expected 32 sub-areas, got \(.["sub-areas"] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
        '

    echo "--- local C++ 1 digit (expect 10x10, total~164311) ---"
    docker run --rm \
        -v "$(pwd)/geotiffs:/geotiffs:ro" \
        -e GEOTIFF_DIR=/geotiffs \
        atgeo-cpp-cli --lonlat -122.3 37.8 \
        | jq --argjson exp_rows 10 --argjson exp_cols 10 \
             --argjson exp_total 164311 --argjson tol 1000 '
        "\(.ulx) \(.uly) \(.dx) \(.dy) \(.data | length) x \(.data[0] | length) total: \(.total)",
        if (.data | length) != $exp_rows then error("expected \($exp_rows) rows, got \(.data | length)") else empty end,
        if (.data[0] | length) != $exp_cols then error("expected \($exp_cols) cols, got \(.data[0] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
        '

    echo "--- local C++ /dgg 6-char geohash (expect 32 sub-areas, total~2936.7) ---"
    docker run --rm \
        -v "$(pwd)/geotiffs:/geotiffs:ro" \
        -e GEOTIFF_DIR=/geotiffs \
        atgeo-cpp-cli --geohash 9q9p1d \
        | jq --argjson exp_total 2936.7 --argjson tol 100 '
        "\(.geohash) total: \(.total) sub-areas: \(.["sub-areas"] | length)",
        if (.["sub-areas"] | length) != 32 then error("expected 32 sub-areas, got \(.["sub-areas"] | length)") else empty end,
        if (.total - $exp_total | fabs) > $tol then error("expected total ~\($exp_total), got \(.total)") else "OK" end
        '

    echo "--- local Python /dgg 7-char quadkey (expect 84 sub-areas) ---"
    GEOTIFF_DIR=geotiffs python lambda.py --quadkey 0230102 \
        | jq '
        "\(.quadkey) total: \(.total) sub-areas: \(.["sub-areas"] | length)",
        if (.["sub-areas"] | length) != 84 then error("expected 84 sub-areas, got \(.["sub-areas"] | length)") else "OK" end
        '
}

smoke_test_local
smoke_test_dd "C++" "${function_cpp_url%/}"
smoke_test_dgg "C++" "${function_cpp_url%/}"
smoke_test_dgg "Python" "${function_url%/}"
smoke_test_quadkey "C++" "${function_cpp_url%/}"
smoke_test_quadkey "Python" "${function_url%/}"
