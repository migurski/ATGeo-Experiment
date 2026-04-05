import argparse
import json
import math
import os
import struct
import unittest
from unittest.mock import MagicMock, patch

from osgeo import gdal

import geohash


def decimal_precision(s):
    if '.' in s:
        return len(s.split('.')[1])
    return 0


def handler(event, context):
    path = event.get('rawPath') or event.get('path') or '/'
    if path == '/dgg':
        return dgg_handler(event, context)

    params = event.get('queryStringParameters') or {}
    lon_str = params.get('lon')
    lat_str = params.get('lat')

    if not lon_str or not lat_str:
        return {'statusCode': 400, 'headers': {'Content-Type': 'text/plain'}, 'body':'Missing lon or lat\n'}

    precision = min(decimal_precision(lon_str), decimal_precision(lat_str))

    if precision >= 4:
        return {'statusCode': 400, 'headers': {'Content-Type': 'text/plain'}, 'body':'ValueError: precision too high (max 3 digits)\n'}

    lon = float(lon_str)
    lat = float(lat_str)
    geotiff_dir = os.environ['GEOTIFF_DIR']

    if precision == 0:
        tif, step, half = 'degree-1digit.tif', 0.1, 0.5
    elif precision == 1:
        tif, step, half = 'degree-2digit.tif', 0.01, 0.05
    elif precision == 2:
        tif, step, half = 'degree-3digit.tif', 0.001, 0.005
    else:  # precision == 3
        tif, step, half = 'degree-3digit.tif', 0.001, 0.0005

    # Round coordinate to detected precision, then build extent
    factor = 10 ** precision
    lon_c = round(round(lon * factor) / factor, precision)
    lat_c = round(round(lat * factor) / factor, precision)
    xmin, xmax = lon_c - half, lon_c + half
    ymin, ymax = lat_c - half, lat_c + half

    ds = gdal.Open(f'{geotiff_dir}/{tif}')
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


def dgg_handler(event, context):
    params = event.get('queryStringParameters') or {}
    gh = params.get('geohash')

    if not gh:
        return {'statusCode': 400, 'headers': {'Content-Type': 'text/plain'}, 'body':'Missing geohash\n'}

    if len(gh) > 7:
        return {'statusCode': 400, 'headers': {'Content-Type': 'text/plain'}, 'body':'ValueError: geohash too long (max 7 characters)\n'}

    if any(c not in geohash.ALPHABET for c in gh):
        return {'statusCode': 400, 'headers': {'Content-Type': 'text/plain'}, 'body':'ValueError: invalid geohash character\n'}

    geotiff_dir = os.environ['GEOTIFF_DIR']
    plon1, plat1, plon2, plat2 = geohash.geohash2lonlats(gh)

    if len(gh) == 7:
        # At max depth, read a single pixel from the same-level TIFF
        tif = f'geohash-{len(gh)}char.tif'
        ds = gdal.Open(f'{geotiff_dir}/{tif}')
        gt = ds.GetGeoTransform()
        lon_c = (plon1 + plon2) / 2
        lat_c = (plat1 + plat2) / 2
        xoff = int((lon_c - gt[0]) / gt[1])
        yoff = int((lat_c - gt[3]) / gt[5])
        raw = ds.GetRasterBand(1).ReadRaster(xoff, yoff, 1, 1)
        ds = None
        (val,) = struct.unpack('f', raw)
        count = 0.0 if math.isnan(val) else round(float(val), 1)
        sub_areas = {gh: {'link': f'/dgg?geohash={gh}', 'count': count}}
        total = count
        dx = plon2 - plon1
        dy = plat1 - plat2  # negative
    else:
        tif = f'geohash-{len(gh) + 1}char.tif'
        ds = gdal.Open(f'{geotiff_dir}/{tif}')
        gt = ds.GetGeoTransform()  # (ulx, dx, 0, uly, 0, dy)

        sub_areas = {}
        total = 0.0

        for c in geohash.ALPHABET:
            child = gh + c
            lon1, lat1, lon2, lat2 = geohash.geohash2lonlats(child)
            lon_c = (lon1 + lon2) / 2
            lat_c = (lat1 + lat2) / 2
            xoff = int((lon_c - gt[0]) / gt[1])
            yoff = int((lat_c - gt[3]) / gt[5])
            raw = ds.GetRasterBand(1).ReadRaster(xoff, yoff, 1, 1)
            (val,) = struct.unpack('f', raw)
            count = 0.0 if math.isnan(val) else round(float(val), 1)
            total += count
            sub_areas[child] = {'link': f'/dgg?geohash={child}', 'count': count}

        ds = None
        # child cell dimensions from first child's bbox
        clon1, clat1, clon2, clat2 = geohash.geohash2lonlats(gh + geohash.ALPHABET[0])
        dx = clon2 - clon1
        dy = clat1 - clat2  # negative

    return {
        'statusCode': 200,
        'headers': {'Content-Type': 'application/json'},
        'body': json.dumps({
            'geohash': gh,
            'ulx': plon1,
            'uly': plat2,
            'dx': dx,
            'dy': dy,
            'total': round(total, 1),
            'sub-areas': sub_areas,
        }) + '\n',
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
# Geohash N-char TIFFs: width=8*4^ceil(N/2), height=4*4^floor(N/2) (approx)
# For mocking we use the 1-char cell size: 360/8 x 180/4 degrees per pixel
_GT = {
    'degree-1digit.tif': (-180.0, 0.1,   0, 90.0, 0, -0.1),
    'degree-2digit.tif': (-180.0, 0.01,  0, 90.0, 0, -0.01),
    'degree-3digit.tif': (-180.0, 0.001, 0, 90.0, 0, -0.001),
    'geohash-2char.tif': (-180.0, 360.0/32, 0, 90.0, 0, -180.0/16),
    'geohash-3char.tif': (-180.0, 360.0/128, 0, 90.0, 0, -180.0/64),
    'geohash-4char.tif': (-180.0, 360.0/512, 0, 90.0, 0, -180.0/256),
    'geohash-5char.tif': (-180.0, 360.0/2048, 0, 90.0, 0, -180.0/1024),
    'geohash-6char.tif': (-180.0, 360.0/8192, 0, 90.0, 0, -180.0/4096),
    'geohash-7char.tif': (-180.0, 360.0/32768, 0, 90.0, 0, -180.0/16384),
    'geohash-8char.tif': (-180.0, 360.0/131072, 0, 90.0, 0, -180.0/65536),
}


def _patched_gdal_open(path, *args, **kwargs):
    tif = path.split('/')[-1]
    return _make_mock_ds(_GT[tif])


class LambdaTests(unittest.TestCase):

    def _call(self, lon, lat):
        event = {'queryStringParameters': {'lon': lon, 'lat': lat}}
        with patch.dict(os.environ, {'GEOTIFF_DIR': '/vsis3/test-bucket'}):
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
        with patch.dict(os.environ, {'GEOTIFF_DIR': '/vsis3/test-bucket'}):
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


class DggHandlerTests(unittest.TestCase):

    def _call(self, gh):
        event = {'rawPath': '/dgg', 'queryStringParameters': {'geohash': gh}}
        with patch.dict(os.environ, {'GEOTIFF_DIR': '/vsis3/test-bucket'}):
            with patch('lambda.gdal.Open', side_effect=_patched_gdal_open):
                return handler(event, None)

    # --- error cases ---

    def test_missing_geohash(self):
        event = {'rawPath': '/dgg', 'queryStringParameters': {}}
        with patch.dict(os.environ, {'GEOTIFF_DIR': '/vsis3/test-bucket'}):
            resp = handler(event, None)
        self.assertEqual(resp['statusCode'], 400)

    def test_geohash_too_long(self):
        resp = self._call('u4pruydq')  # 8 chars
        self.assertEqual(resp['statusCode'], 400)

    def test_invalid_geohash_char(self):
        resp = self._call('u4a')  # 'a' not in ALPHABET
        self.assertEqual(resp['statusCode'], 400)

    # --- success cases ---

    def test_returns_32_sub_areas(self):
        resp = self._call('u')
        self.assertEqual(resp['statusCode'], 200)
        body = json.loads(resp['body'])
        self.assertEqual(len(body['sub-areas']), 32)

    def test_sub_area_keys_are_children(self):
        resp = self._call('u4p')
        body = json.loads(resp['body'])
        for key in body['sub-areas']:
            self.assertTrue(key.startswith('u4p'))
            self.assertEqual(len(key), 4)

    def test_response_keys(self):
        resp = self._call('u')
        body = json.loads(resp['body'])
        for key in ('geohash', 'ulx', 'uly', 'dx', 'dy', 'total', 'sub-areas'):
            self.assertIn(key, body)

    def test_bbox_values(self):
        resp = self._call('u')
        body = json.loads(resp['body'])
        # 'u' covers lon 0..45, lat 45..90
        self.assertAlmostEqual(body['ulx'], 0.0, places=6)
        self.assertAlmostEqual(body['uly'], 90.0, places=6)
        self.assertGreater(body['dx'], 0)
        self.assertLess(body['dy'], 0)

    def test_sub_area_entry_shape(self):
        resp = self._call('u')
        body = json.loads(resp['body'])
        entry = next(iter(body['sub-areas'].values()))
        self.assertIn('link', entry)
        self.assertIn('count', entry)

    def test_seven_char_geohash(self):
        resp = self._call('u4pruyd')
        self.assertEqual(resp['statusCode'], 200)
        body = json.loads(resp['body'])
        self.assertEqual(len(body['sub-areas']), 1)
        self.assertIn('u4pruyd', body['sub-areas'])

    def test_dispatch_via_path(self):
        # /dgg path routes to dgg_handler; / path does not
        event_dgg = {'rawPath': '/dgg', 'queryStringParameters': {'geohash': 'u'}}
        event_root = {'rawPath': '/', 'queryStringParameters': {}}
        with patch.dict(os.environ, {'GEOTIFF_DIR': '/vsis3/test-bucket'}):
            with patch('lambda.gdal.Open', side_effect=_patched_gdal_open):
                resp_dgg = handler(event_dgg, None)
            resp_root = handler(event_root, None)
        self.assertEqual(resp_dgg['statusCode'], 200)
        self.assertEqual(resp_root['statusCode'], 400)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--lonlat', nargs=2, type=float, metavar=('LON', 'LAT'))
    group.add_argument('--geohash', type=str)
    args = parser.parse_args()

    if args.lonlat:
        lon, lat = args.lonlat
        event = {'queryStringParameters': {'lon': str(lon), 'lat': str(lat)}}
    else:
        event = {'rawPath': '/dgg', 'queryStringParameters': {'geohash': args.geohash}}

    resp = handler(event, None)
    print(resp['body'], end='')
