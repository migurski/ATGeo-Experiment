import json
import math
import os
import struct

from osgeo import gdal


def decimal_precision(s):
    if '.' in s:
        return len(s.split('.')[1])
    return 0


def handler(event, context):
    params = event.get('queryStringParameters') or {}
    lon_str = params.get('lon')
    lat_str = params.get('lat')

    if not lon_str or not lat_str:
        return {'statusCode': 400, 'body': 'Missing lon or lat\n'}

    precision = min(decimal_precision(lon_str), decimal_precision(lat_str))

    if precision >= 4:
        return {'statusCode': 400, 'body': 'ValueError: precision too high (max 3 digits)\n'}

    lon = float(lon_str)
    lat = float(lat_str)
    bucket = os.environ['DATA_BUCKET_NAME']

    if precision == 0:
        tif, step, half = 'hrsl-1.tif', 0.1, 0.5
    elif precision == 1:
        tif, step, half = 'hrsl-2.tif', 0.01, 0.05
    elif precision == 2:
        tif, step, half = 'hrsl-3.tif', 0.001, 0.005
    else:  # precision == 3
        tif, step, half = 'hrsl-3.tif', 0.001, 0.0005

    # Round coordinate to detected precision, then build extent
    factor = 10 ** precision
    lon_c = round(round(lon * factor) / factor, precision)
    lat_c = round(round(lat * factor) / factor, precision)
    xmin, xmax = lon_c - half, lon_c + half
    ymin, ymax = lat_c - half, lat_c + half

    ds = gdal.Open(f'/vsis3/{bucket}/{tif}')
    gt = ds.GetGeoTransform()  # (ulx, dx, 0, uly, 0, dy)
    xoff = round((xmin - gt[0]) / gt[1])
    yoff = round((ymax - gt[3]) / gt[5])
    xsize = round((xmax - xmin) / gt[1])
    ysize = round((ymin - ymax) / gt[5])
    raw = ds.GetRasterBand(1).ReadRaster(xoff, yoff, xsize, ysize)
    ds = None

    floats = struct.unpack(f'{xsize * ysize}f', raw)
    matrix = [
        [None if math.isnan(v) else float(v) for v in floats[row * xsize:(row + 1) * xsize]]
        for row in range(ysize)
    ]

    prec = precision + 1
    return {
        'statusCode': 200,
        'headers': {'Content-Type': 'application/json'},
        'body': json.dumps(dict(
            ulx=round(xmin, prec),
            uly=round(ymax, prec),
            dx=step,
            dy=-step,
            data=matrix,
        )) + '\n',
    }
