import json
import time
import urllib.request

def handler(event, context):
    start_time = time.time()
    path = event['requestContext']['http']['path']
    got = urllib.request.urlopen(f'https://planscore.s3.amazonaws.com{path}')
    data = json.load(got)
    elapsed = round((time.time()  - start_time) * 1000, 2)
    response = dict(elapsed=elapsed, description=data['description'], message=data['message'])
    return {
        'statusCode': 200,
        'headers': {'Content-Type': 'application/json'},
        'body': json.dumps(response) + '\n'
    }
