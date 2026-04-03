#!/bin/bash -ex

cd "$(dirname "$0")"

docker build --platform linux/arm64 --target builder --build-arg CACHE_BUST="$(date +%s)" -t atgeo-cpp-builder .

CONTAINER_ID=$(docker create atgeo-cpp-builder sh)
docker cp "${CONTAINER_ID}:/lambda-cpp.zip" ../lambda-cpp.zip
docker rm "${CONTAINER_ID}"

echo "Built: ../lambda-cpp.zip"
unzip -l ../lambda-cpp.zip
