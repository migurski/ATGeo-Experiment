all: geotiffs/hrsl-3.tif geotiffs/hrsl-2.tif geotiffs/hrsl-1.tif

# Original data: https://registry.opendata.aws/dataforgood-fb-hrsl/

geotiffs/hrsl-3.tif:
	mkdir -p geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
		-tr 0.001 -0.001 -r sum -te -180 -90 180 90 \
		/vsis3/dataforgood-fb-data/hrsl-cogs/hrsl_general/hrsl_general-latest.vrt geotiffs/hrsl-3.tif

geotiffs/hrsl-2.tif: geotiffs/hrsl-3.tif
	mkdir -p geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -tr 0.01 -0.01 -r sum -te -180 -90 180 90 \
	    geotiffs/hrsl-3.tif geotiffs/hrsl-2.tif

geotiffs/hrsl-1.tif: geotiffs/hrsl-2.tif
	mkdir -p geotiffs
	gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
	    -tr 0.1 -0.1 -r sum -te -180 -90 180 90 \
	    geotiffs/hrsl-2.tif geotiffs/hrsl-1.tif

.PHONY: all