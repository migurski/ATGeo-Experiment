[API Docs](openapi.yaml)

## Interaction

Let’s say you’re at -122.27119° Longitude, 37.80432° Latitude, the intersection of
[14th & Broadway in Downtown Oakland](https://www.openstreetmap.org/node/10139029526).

### Zero Decimal Degrees or 100km Precision

That rounds to (-122, 38), so your first request is for
[https://atgeo-experiment.teczno.com/?lon=-122&lat=38](https://atgeo-experiment.teczno.com/?lon=-122&lat=38),
with this response:

```JSON
{
    "ulx": -122.5,
    "uly": 38.5,
    "dx": 0.1,
    "dy": -0.1,
    "total": 4535260,
    "data": [ …, [ 45967.3, 45967.3, 302894, … ], … ]
}
```

Using whole number degrees in this area could identify you as one of 4.5 million
people in the densely populated northern Bay Area. Inside the matrix of sub-areas, the
one with your real downtown Oakland location has almost 303K people, a comfortably large
number as well. It’s safe to look deeper.

<a href="https://www.openstreetmap.org/#map=10/37.80432/-122.27119"><img src="img/hrsl-1.png" width="100%"></a>

### One Decimal Degree or 10km Precision

You add a degree of precision to get (-122.3, 37.8) and make your second request for
[https://atgeo-experiment.teczno.com/?lon=-122.3&lat=37.8](https://atgeo-experiment.teczno.com/?lon=-122.3&lat=37.8),
with this response:

```JSON
{
    "ulx": -122.35,
    "uly": 38.85,
    "dx": 0.01,
    "dy": -0.01,
    "total": 164311,
    "data": [ …, [ …, 6771.7, 6570, 3894.2 ], … ]
}
```

The total here is 164K, different from the earlier 303K because this area is aligned a
little to the west between (-122.35, 37.75) and (-122.25, 37.85). Your real downtown
Oakland location is in one of the more heavily populated sub-areas with 6.8K people,
which also seems comfortably large. It’s safe to look deeper again.

<a href="https://www.openstreetmap.org/#map=13/37.80432/-122.27119"><img src="img/hrsl-2.png" width="100%"></a>

### Two Decimal Degrees or 1km Precision

You add a degree of precision including a significant zero to get (-122.27, 37.80) and make your third request for
[https://atgeo-experiment.teczno.com/?lon=-122.27&lat=37.80](https://atgeo-experiment.teczno.com/?lon=-122.27&lat=37.80),
with this response:

```JSON
{
    "ulx": -122.275,
    "uly": 38.805,
    "dx": 0.001,
    "dy": -0.001,
    "total": 7198.8,
    "data": [ [ null, null, null, 5.3, … ], … ]
}
```

The total here is 7.2K for an area between (-122.275, 38.795) and (-122.285, 38.805).
Your real downtown location now has just 5 people in it, and the densest areas here have
just a few hundred. Each sub-area is just 100m long from North to South and we’re
reaching the limits of HRSL precision. You’re not comfortable geolocating yourself any
further than two degrees of decimal precision because it gives away too much detail so
you stop.

<a href="https://www.openstreetmap.org/#map=16/37.80432/-122.27119"><img src="img/hrsl-3.png" width="100%"></a>

## Geohash Drill-Down with `/dgg`

The `/dgg` endpoint accepts a geohash string (1–7 characters) and returns the 32 next-level
sub-areas corresponding to each character in the geohash base-32 alphabet.

Let's use the same location, 14th & Broadway in Downtown Oakland, whose geohash is `u4pruydqqvj8`.

### One-Character Geohash (~5000km)

`u` covers most of northern Europe and Russia. Request
`/dgg?geohash=u` to see all 32 two-character children:

```JSON
{
    "geohash": "u",
    "total": 234567890.0,
    "sub-areas": {
        "u0": {"link": "/dgg?geohash=u0", "count": 1234567.0},
        "u1": {"link": "/dgg?geohash=u1", "count": 987654.0},
        ...
    }
}
```

### Three-Character Geohash (~150km)

`u4p` covers an area around Oslo, Norway. Request `/dgg?geohash=u4p`
to see 32 four-character children, each roughly 40km across:

```JSON
{
    "geohash": "u4p",
    "total": 2345678.0,
    "sub-areas": {
        "u4p0": {"link": "/dgg?geohash=u4p0", "count": 12345.0},
        "u4p1": {"link": "/dgg?geohash=u4p1", "count": 0.0},
        ...
    }
}
```

### Seven-Character Geohash (~150m)

`u4pruyd` narrows to a neighbourhood-scale cell. Request `/dgg?geohash=u4pruyd`
to see 32 eight-character children. At this scale individual cells are roughly
20m across — approaching the limit of HRSL precision — so most child counts
will be small or zero.
