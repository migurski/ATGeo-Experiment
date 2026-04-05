all: \
	geotiffs/degree-3digit.tif geotiffs/degree-2digit.tif geotiffs/degree-1digit.tif \
	geotiffs/geohash-7char.tif geotiffs/geohash-6char.tif geotiffs/geohash-5char.tif \
	geotiffs/geohash-4char.tif geotiffs/geohash-3char.tif geotiffs/geohash-2char.tif \
	geotiffs/geohash-1char.tif \
	geotiffs/quadkey-19char.tif geotiffs/quadkey-18char.tif geotiffs/quadkey-17char.tif \
	geotiffs/quadkey-16char.tif geotiffs/quadkey-15char.tif geotiffs/quadkey-14char.tif \
	geotiffs/quadkey-13char.tif geotiffs/quadkey-12char.tif geotiffs/quadkey-11char.tif \
	geotiffs/quadkey-10char.tif geotiffs/quadkey-9char.tif geotiffs/quadkey-8char.tif \
	geotiffs/quadkey-7char.tif geotiffs/quadkey-6char.tif geotiffs/quadkey-5char.tif \
	geotiffs/quadkey-4char.tif geotiffs/quadkey-3char.tif geotiffs/quadkey-2char.tif \
	geotiffs/quadkey-1char.tif

geotiffs:
	mkdir -p geotiffs

# Original data: https://registry.opendata.aws/dataforgood-fb-hrsl/

geotiffs/degree-3digit.tif: geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
		-tr 0.001 -0.001 -r sum -te -180 -90 180 90 \
		/vsis3/dataforgood-fb-data/hrsl-cogs/hrsl_general/hrsl_general-latest.vrt $@

geotiffs/degree-2digit.tif: geotiffs/degree-3digit.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -tr 0.01 -0.01 -r sum \
	    $< $@

geotiffs/degree-1digit.tif: geotiffs/degree-2digit.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -tr 0.1 -0.1 -r sum \
	    $< $@

# Geohash

geotiffs/geohash-7char.tif: geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 262144 131072 -r sum -te -180 -90 180 90 \
	    /tmp/hrsl/hrsl_general-latest.vrt $@

geotiffs/geohash-6char.tif: geotiffs/geohash-7char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 32768 32768 -r sum \
	    $< $@

geotiffs/geohash-5char.tif: geotiffs/geohash-6char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 8192 4096 -r sum \
	    $< $@

geotiffs/geohash-4char.tif: geotiffs/geohash-5char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 1024 1024 -r sum \
	    $< $@

geotiffs/geohash-3char.tif: geotiffs/geohash-4char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 256 128 -r sum \
	    $< $@

geotiffs/geohash-2char.tif: geotiffs/geohash-3char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 32 32 -r sum \
	    $< $@

geotiffs/geohash-1char.tif: geotiffs/geohash-2char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 8 4 -r sum \
	    $< $@

# Quadkey

geotiffs/quadkey-18char.tif: geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 262144 262144 -r sum -te -20037508.34 -20037508.34 20037508.34 20037508.34 -t_srs EPSG:3857 \
	    /tmp/hrsl/hrsl_general-latest.vrt $@

geotiffs/quadkey-17char.tif: geotiffs/quadkey-18char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 131072 131072 -r sum \
	    $< $@

geotiffs/quadkey-16char.tif: geotiffs/quadkey-17char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 65536 65536 -r sum \
	    $< $@

geotiffs/quadkey-15char.tif: geotiffs/quadkey-16char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 32768 32768 -r sum \
	    $< $@

geotiffs/quadkey-14char.tif: geotiffs/quadkey-15char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 16384 16384 -r sum \
	    $< $@

geotiffs/quadkey-13char.tif: geotiffs/quadkey-14char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 8192 8192 -r sum \
	    $< $@

geotiffs/quadkey-12char.tif: geotiffs/quadkey-13char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 4096 4096 -r sum \
	    $< $@

geotiffs/quadkey-11char.tif: geotiffs/quadkey-12char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 2048 2048 -r sum \
	    $< $@

geotiffs/quadkey-10char.tif: geotiffs/quadkey-11char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 1024 1024 -r sum \
	    $< $@

geotiffs/quadkey-9char.tif: geotiffs/quadkey-10char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 512 512 -r sum \
	    $< $@

geotiffs/quadkey-8char.tif: geotiffs/quadkey-9char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 256 256 -r sum \
	    $< $@

geotiffs/quadkey-7char.tif: geotiffs/quadkey-8char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 128 128 -r sum \
	    $< $@

geotiffs/quadkey-6char.tif: geotiffs/quadkey-7char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 64 64 -r sum \
	    $< $@

geotiffs/quadkey-5char.tif: geotiffs/quadkey-6char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 32 32 -r sum \
	    $< $@

geotiffs/quadkey-4char.tif: geotiffs/quadkey-5char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 16 16 -r sum \
	    $< $@

geotiffs/quadkey-3char.tif: geotiffs/quadkey-4char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 8 8 -r sum \
	    $< $@

geotiffs/quadkey-2char.tif: geotiffs/quadkey-3char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 4 4 -r sum \
	    $< $@

geotiffs/quadkey-1char.tif: geotiffs/quadkey-2char.tif geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -ts 2 2 -r sum \
	    $< $@

.PHONY: all
