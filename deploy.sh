#!/bin/bash -ex

bootstrap_stack_name='atgeo-experiment-bootstrap'
app_stack_name='atgeo-experiment'

# Phase 1: Deploy bootstrap stack
aws cloudformation deploy \
    --stack-name "${bootstrap_stack_name}" \
    --template-file bootstrap-template.yaml \
    --no-fail-on-empty-changeset

# Phase 2: Retrieve deployment bucket name
deployment_bucket_name=$(aws cloudformation describe-stacks \
    --stack-name "${bootstrap_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='DeploymentBucketName'].OutputValue" \
    --output text)

echo "Deployment bucket: ${deployment_bucket_name}"

# Phase 3: Build lambda.zip
rm -f lambda.zip
zip -j lambda.zip lambda.py

# Phase 4: Upload lambda.zip to S3
aws s3 cp lambda.zip "s3://${deployment_bucket_name}/lambda.zip"

# Phase 4b: Stage geotiffs to bootstrap bucket
aws s3 sync geotiffs/ "s3://${deployment_bucket_name}/geotiffs/"

# Phase 5: Deploy application stack
aws cloudformation deploy \
    --stack-name "${app_stack_name}" \
    --template-file application-template.yaml \
    --capabilities CAPABILITY_NAMED_IAM \
    --parameter-overrides \
        BootstrapStackName="${bootstrap_stack_name}" \
        LambdaPackageKey='lambda.zip' \
    --no-fail-on-empty-changeset

# Phase 6: Sync geotiffs to data bucket
data_bucket_name=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='DataBucketName'].OutputValue" \
    --output text)

echo "Data bucket: ${data_bucket_name}"

aws s3 sync "s3://${deployment_bucket_name}/geotiffs/" "s3://${data_bucket_name}/"

# Phase 7: Print function URL
function_url=$(aws cloudformation describe-stacks \
    --stack-name "${app_stack_name}" \
    --query "Stacks[0].Outputs[?OutputKey=='FunctionUrl'].OutputValue" \
    --output text)

echo '----'
echo "Function URL: ${function_url}"
