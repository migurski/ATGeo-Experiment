from __future__ import annotations

import math
import unittest

EPSILON = 1e-14
PRECISION = 20

def binpad(i: int|float, k: int) -> str:
    ''' Convert number to base-2 integer zero-padded to length k
    '''
    fmt = '{:>0' + str(k) + 's}'
    return fmt.format(bin(int(i))[2:])

def lonlat2quadkey(lon: float, lat: float) -> str:
    ''' Convert (lon, lat) pair to a max-precision quadkey string
    
    Implemention copyright 2021, Mapbox
    https://github.com/mapbox/mercantile/blob/main/LICENSE.txt
    '''
    x = lon / 360.0 + 0.5
    sinlat = math.sin(math.radians(lat))
    y = 0.5 - 0.25 * math.log((1.0 + sinlat) / (1.0 - sinlat)) / math.pi

    Z2 = 2 ** PRECISION
    xtile = int(math.floor((x + EPSILON) * Z2))
    ytile = int(math.floor((y + EPSILON) * Z2))

    xbin, ybin = binpad(xtile, PRECISION), binpad(ytile, PRECISION)
    chars = [str(int(y+x, 2)) for y, x in zip(ybin, xbin)]
    quadkey = ''.join(chars)

    return quadkey

def quadkey2lonlats(key: str) -> tuple[float, float, float, float]:
    ''' Convert quadkey string to a (lon1, lat1, lon2, lat2) range
    '''
    xychars = [binpad(int(c), 2) for c in key]
    ybin, xbin = [''.join(chars) for chars in zip(*xychars)]

    Z2 = 2 ** len(key)
    x1, x2 = int(xbin, 2) / Z2, (1 + int(xbin, 2)) / Z2
    y1, y2 = (1 + int(ybin, 2)) / Z2, int(ybin, 2) / Z2

    lon1, lon2 = (x1 - 0.5) * 360.0, (x2 - 0.5) * 360.0
    y1b, y2b = (0.5 - y1) * 2 * math.pi, (0.5 - y2) * 2 * math.pi
    lat1 = math.degrees(2 * math.atan(math.exp(y1b)) - 0.5 * math.pi)
    lat2 = math.degrees(2 * math.atan(math.exp(y2b)) - 0.5 * math.pi)
    
    return lon1, lat1, lon2, lat2

class TestCase (unittest.TestCase):

    def test_binpad(self):
        self.assertEqual(binpad(12345, 20), '00000011000000111001')

    def test_lonlat2quadkey(self):
        key = lonlat2quadkey(-122.27119, 37.80432)
        self.assertTrue(key.startswith('0230102122203301'))

    def test_quadkey2lonlats(self):
        lon1, lat1, lon2, lat2 = quadkey2lonlats('02301021222033010211')
        self.assertTrue(lon1 <= -122.27119 < lon2)
        self.assertTrue(lat1 <= 37.80432 < lat2)
