#!/bin/bash -ex

bootstrap_stack_name='atgeo-experiment-bootstrap'
app_stack_name='atgeo-experiment'

# Phase 1: Deploy bootstrap stack
aws cloudformation deploy \
    --stack-name "${bootstrap_stack_name}" \
    --template-file bootstrap-template.yaml \
    --no-fail-on-empty-changeset

# Phase 2: Retrieve bootstrap stack outputs
deployment_bucket_name=$(aws cloudformation describe-stacks \
    --stack-name "${bootstrap_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='DeploymentBucketName'].OutputValue" \
    --output text)

image_repository_uri=$(aws cloudformation describe-stacks \
    --stack-name "${bootstrap_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='ImageRepositoryUri'].OutputValue" \
    --output text)

echo "Deployment bucket: ${deployment_bucket_name}"
echo "Image repository: ${image_repository_uri}"

# Phase 3: Build Docker image for ARM64
image_uri="${image_repository_uri}:latest"

docker buildx build \
    --platform linux/arm64 \
    --tag "${image_uri}" \
    --load \
    .

# Phase 4: Push image to ECR
aws_region=$(aws configure get region)
aws_account_id=$(aws sts get-caller-identity --query Account --output text)

aws ecr get-login-password --region "${aws_region}" \
    | docker login \
        --username AWS \
        --password-stdin \
        "${aws_account_id}.dkr.ecr.${aws_region}.amazonaws.com"

docker push "${image_uri}"

# Resolve digest so CloudFormation always sees a change when the image changes
image_digest=$(docker inspect --format='{{index .RepoDigests 0}}' "${image_uri}")
image_uri="${image_digest}"

# Phase 4b: Stage geotiffs to bootstrap bucket
aws s3 sync geotiffs/ "s3://${deployment_bucket_name}/geotiffs/"

# Phase 4c: Build C++ Lambda and upload to deployment bucket
bash cpp/build.sh
cpp_hash=$(md5 -q lambda-cpp.zip)
cpp_package_key="lambda-cpp-${cpp_hash}.zip"
aws s3 cp lambda-cpp.zip "s3://${deployment_bucket_name}/${cpp_package_key}"

# Phase 5: Deploy application stack
aws cloudformation deploy \
    --stack-name "${app_stack_name}" \
    --template-file application-template.yaml \
    --capabilities CAPABILITY_NAMED_IAM \
    --parameter-overrides \
        BootstrapStackName="${bootstrap_stack_name}" \
        ImageUri="${image_uri}" \
        CppPackageKey="${cpp_package_key}" \
    --no-fail-on-empty-changeset

# Phase 6: Sync geotiffs to data bucket
data_bucket_name=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='DataBucketName'].OutputValue" \
    --output text)

echo "Data bucket: ${data_bucket_name}"

aws s3 sync "s3://${deployment_bucket_name}/geotiffs/" "s3://${data_bucket_name}/"

# Phase 7: Print function URLs
function_cpp_url=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='FunctionCppUrl'].OutputValue" \
    --output text)

function_cpp_custom_domain=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='FunctionCppCustomDomain'].OutputValue" \
    --output text)

cloudfront_domain=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='CppCloudFrontDomain'].OutputValue" \
    --output text)

echo '----'
echo "Function URL (C++ direct):  ${function_cpp_url}"
echo "Function URL (C++ custom):  ${function_cpp_custom_domain}"
echo "CloudFront domain (CNAME):  ${cloudfront_domain}"

# Phase 8: Smoke tests
smoke_test() {
    local label="$1"
    local url="$2"

    echo "--- ${label} 0 digits (expect 10x10) ---"
    curl -sf "${url}?lon=-122&lat=38" \
        | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ulx'], d['uly'], d['dx'], d['dy'], len(d['data']), 'x', len(d['data'][0]))"

    echo "--- ${label} 1 digit (expect 10x10) ---"
    curl -sf "${url}?lon=-122.3&lat=37.8" \
        | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ulx'], d['uly'], d['dx'], d['dy'], len(d['data']), 'x', len(d['data'][0]))"

    echo "--- ${label} 2 digits (expect 10x10) ---"
    curl -sf "${url}?lon=-122.27&lat=37.80" \
        | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ulx'], d['uly'], d['dx'], d['dy'], len(d['data']), 'x', len(d['data'][0]))"

    echo "--- ${label} 3 digits (expect 1x1) ---"
    curl -sf "${url}?lon=-122.271&lat=37.804" \
        | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ulx'], d['uly'], d['dx'], d['dy'], len(d['data']), 'x', len(d['data'][0]))"

    echo "--- ${label} 4 digits (expect HTTP 400) ---"
    curl -s -o /dev/null -w "%{http_code}" "${url}?lon=-122.2713&lat=37.8043"
    echo
}

smoke_test "C++" "${function_cpp_custom_domain}"
