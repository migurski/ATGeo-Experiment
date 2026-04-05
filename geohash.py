from __future__ import annotations

import unittest

# https://en.wikipedia.org/wiki/Geohash#Textual_representation
ALPHABET = '0123456789bcdefghjkmnpqrstuvwxyz'

# 14 = 35 * 2 / 5 geohash characters
PRECISION = 35

def binpad(i: int|float, k: int) -> str:
    ''' Convert number to base-2 integer zero-padded to length k
    '''
    fmt = '{:>0' + str(k) + 's}'
    return fmt.format(bin(int(i))[2:])

def lonlat2geohash(lon: float, lat: float) -> str:
    ''' Convert (lon, lat) pair to a max-precision geohash string
    '''
    x, y = (lon + 180) / 360, (lat + 90) / 180
    scale = 2 ** PRECISION
    xbin, ybin = binpad(scale * x, PRECISION), binpad(scale * y, PRECISION)
    xybin = ''.join(x + y for x, y in zip(xbin, ybin))
    xychars = [xybin[i:i+5] for i in range(0, len(xybin), 5)]
    geohash = ''.join(ALPHABET[int(c, 2)] for c in xychars)

    return geohash

def geohash2lonlats(geohash: str) -> tuple[float, float, float, float]:
    ''' Convert geohash string to a (lon1, lat1, lon2, lat2) range
    '''
    alphabet_codes = {c: binpad(i, 5) for i, c in enumerate(ALPHABET)}
    xybin = ''.join(alphabet_codes[c] for c in geohash)
    xbin, ybin = xybin[0::2], xybin[1::2]
    xden, yden = 2 ** len(xbin), 2 ** len(ybin)
    x1, x2 = int(xbin, 2) / xden, (1 + int(xbin, 2)) / xden
    y1, y2 = int(ybin, 2) / yden, (1 + int(ybin, 2)) / yden
    lat1, lon1 = y1 * 180 - 90, x1 * 360 - 180
    lat2, lon2 = y2 * 180 - 90, x2 * 360 - 180

    return lon1, lat1, lon2, lat2

class TestCase (unittest.TestCase):

    def test_binpad(self):
        self.assertEqual(binpad(12345, 20), '00000011000000111001')

    def test_lonlat2geohash(self):
        hash = lonlat2geohash(10.40744, 57.64911)
        self.assertTrue(hash.startswith('u4pruydqqvj8'))

    def test_geohash2lonlats(self):
        lon1, lat1, lon2, lat2 = geohash2lonlats('u4pruydqqvj8')
        self.assertTrue(lon1 <= 10.40744 < lon2)
        self.assertTrue(lat1 <= 57.64911 < lat2)
