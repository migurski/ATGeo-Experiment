import json
import math
import os
import struct
import unittest
from unittest.mock import MagicMock, patch

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
    total = round(sum(0.0 if math.isnan(v) else float(v) for v in floats), 1)
    matrix = [
        [None if math.isnan(v) else round(float(v), 1) for v in floats[row * xsize:(row + 1) * xsize]]
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
            total=total,
            data=matrix,
        )) + '\n',
    }


def _make_mock_ds(gt):
    """Return a mock GDAL dataset with given GeoTransform and ReadRaster returning 1.5 floats."""
    def fake_read_raster(xoff, yoff, xsize, ysize):
        return struct.pack(f'{xsize * ysize}f', *([1.5] * (xsize * ysize)))

    band = MagicMock()
    band.ReadRaster.side_effect = fake_read_raster

    ds = MagicMock()
    ds.GetGeoTransform.return_value = gt
    ds.GetRasterBand.return_value = band
    return ds


# GeoTransforms matching each HRSL file's pixel size
_GT = {
    'hrsl-1.tif': (-180.0, 0.1,   0, 90.0, 0, -0.1),
    'hrsl-2.tif': (-180.0, 0.01,  0, 90.0, 0, -0.01),
    'hrsl-3.tif': (-180.0, 0.001, 0, 90.0, 0, -0.001),
}


def _patched_gdal_open(path, *args, **kwargs):
    tif = path.split('/')[-1]
    return _make_mock_ds(_GT[tif])


class LambdaTests(unittest.TestCase):

    def _call(self, lon, lat):
        event = {'queryStringParameters': {'lon': lon, 'lat': lat}}
        with patch.dict(os.environ, {'DATA_BUCKET_NAME': 'test-bucket'}):
            with patch('lambda.gdal.Open', side_effect=_patched_gdal_open):
                return handler(event, None)

    # --- decimal_precision ---

    def test_precision_integer(self):
        self.assertEqual(decimal_precision('38'), 0)
        self.assertEqual(decimal_precision('-122'), 0)

    def test_precision_one_digit(self):
        self.assertEqual(decimal_precision('37.8'), 1)

    def test_precision_two_digits_trailing_zero(self):
        self.assertEqual(decimal_precision('37.80'), 2)

    def test_precision_three_digits(self):
        self.assertEqual(decimal_precision('-122.271'), 3)

    # --- handler: error cases ---

    def test_missing_params(self):
        with patch.dict(os.environ, {'DATA_BUCKET_NAME': 'test-bucket'}):
            resp = handler({}, None)
        self.assertEqual(resp['statusCode'], 400)

    def test_precision_too_high(self):
        resp = self._call('-122.2713', '37.8043')
        self.assertEqual(resp['statusCode'], 400)

    # --- handler: shape checks ---

    def test_zero_digits_10x10(self):
        resp = self._call('-122', '38')
        self.assertEqual(resp['statusCode'], 200)
        data = json.loads(resp['body'])['data']
        self.assertEqual(len(data), 10)
        self.assertEqual(len(data[0]), 10)

    def test_one_digit_10x10(self):
        resp = self._call('-122.3', '37.8')
        self.assertEqual(resp['statusCode'], 200)
        data = json.loads(resp['body'])['data']
        self.assertEqual(len(data), 10)
        self.assertEqual(len(data[0]), 10)

    def test_two_digits_10x10(self):
        resp = self._call('-122.27', '37.80')
        self.assertEqual(resp['statusCode'], 200)
        data = json.loads(resp['body'])['data']
        self.assertEqual(len(data), 10)
        self.assertEqual(len(data[0]), 10)

    def test_three_digits_1x1(self):
        resp = self._call('-122.271', '37.804')
        self.assertEqual(resp['statusCode'], 200)
        data = json.loads(resp['body'])['data']
        self.assertEqual(len(data), 1)
        self.assertEqual(len(data[0]), 1)

    # --- handler: envelope keys present ---

    def test_envelope_keys(self):
        resp = self._call('-122.3', '37.8')
        body = json.loads(resp['body'])
        for key in ('ulx', 'uly', 'dx', 'dy', 'total', 'data'):
            self.assertIn(key, body)


if __name__ == '__main__':
    unittest.main()
