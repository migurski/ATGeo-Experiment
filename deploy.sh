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

# Phase 5: Deploy application stack
aws cloudformation deploy \
    --stack-name "${app_stack_name}" \
    --template-file application-template.yaml \
    --capabilities CAPABILITY_NAMED_IAM \
    --parameter-overrides \
        BootstrapStackName="${bootstrap_stack_name}" \
        ImageUri="${image_uri}" \
    --no-fail-on-empty-changeset

# Phase 6: Sync geotiffs to data bucket
data_bucket_name=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='DataBucketName'].OutputValue" \
    --output text)

echo "Data bucket: ${data_bucket_name}"

aws s3 sync "s3://${deployment_bucket_name}/geotiffs/" "s3://${data_bucket_name}/"

# Phase 7: Print function URLs
function_url=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='FunctionUrl'].OutputValue" \
    --output text)

cloudfront_domain=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='CloudFrontDomain'].OutputValue" \
    --output text)

echo '----'
echo "Function URL (Python direct): ${function_url}"
echo "Custom domain:                https://atgeo-experiment.teczno.com/"
echo "CloudFront domain (CNAME):    ${cloudfront_domain}"

# Phase 8: Smoke tests
bash smoke-test.sh "${app_stack_name}"
