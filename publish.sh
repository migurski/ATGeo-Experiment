#!/bin/bash -ex
rm -f lambda.zip
zip -j lambda.zip lambda.py

( aws lambda get-function --function-name atgeo-experiment ) \
    && EXISTS='yes' \
    || EXISTS='no'

if [ $EXISTS = 'yes' ]; then
    aws lambda update-function-code \
        --function-name atgeo-experiment \
        --zip-file fileb://lambda.zip \
     | jq -c .
else
    aws lambda create-function \
        --function-name atgeo-experiment \
        --runtime python3.14 \
        --role arn:aws:iam::101696101272:role/Clanker \
        --handler lambda.handler \
        --zip-file fileb://lambda.zip \
     | jq -c .
    
    aws lambda add-permission \
        --function-name atgeo-experiment \
        --statement-id FunctionURLAllowPublicAccess \
        --action lambda:InvokeFunctionUrl \
        --principal "*" \
        --function-url-auth-type NONE \
     | jq -c .

    aws lambda create-function-url-config \
        --function-name atgeo-experiment \
        --auth-type NONE \
     | jq -c .
fi

URL=`aws lambda get-function-url-config --function-name atgeo-experiment | jq -r .FunctionUrl`

echo '----'
echo 'URL:' $URL

sleep 3

curl -s "${URL}uploads/20251220T215350.341663516Z/index.json"
