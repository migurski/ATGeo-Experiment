import json
import os
from osgeo import gdal

def handler(event, context):
    path = event['requestContext']['http']['path'].lstrip('/')
    bucket = os.environ['DATA_BUCKET_NAME']

    ds = gdal.Open(f'/vsis3/{bucket}/{path}')
    info = dict(
        key=path,
        width=ds.RasterXSize,
        height=ds.RasterYSize,
        bands=ds.RasterCount,
        projection=ds.GetProjection(),
    )
    ds = None

    return {
        'statusCode': 200,
        'headers': {'Content-Type': 'application/json'},
        'body': json.dumps(info) + '\n',
    }
