# Progress

## CloudFormation deployment setup

Replaced the manual `publish.sh` AWS CLI script with a proper CloudFormation-based deployment pipeline.

### What was added

- **`bootstrap-template.yaml`** — CloudFormation stack `atgeo-experiment-bootstrap` that creates a versioned, private S3 bucket (`atgeo-experiment-packages-{region}-{account}`) for storing Lambda deployment packages.

- **`application-template.yaml`** — CloudFormation stack `atgeo-experiment` that creates:
  - IAM execution role with `AWSLambdaBasicExecutionRole`
  - Lambda function (`python3.14`, handler `lambda.handler`, code loaded from S3)
  - Public Lambda function URL (auth-type NONE)

- **`deploy.sh`** — Six-phase deployment script:
  1. Deploy bootstrap stack
  2. Retrieve S3 bucket name from stack outputs
  3. Build `lambda.zip`
  4. Upload `lambda.zip` to S3
  5. Deploy application stack
  6. Print the live function URL

### Lambda function

`lambda.py` proxies HTTP requests to PlanScore S3, fetching plan score JSON by path and returning `elapsed`, `description`, and `message` fields.

Live URL: `https://td2ffxwrvcpungbeomitfoha4u0lrwox.lambda-url.us-east-1.on.aws/`
