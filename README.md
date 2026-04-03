Install GDAL (does not work with newest pip):

    pip3 install \
        --global-option=build_ext \
        --global-option="-I/Library/Frameworks/GDAL.framework/Versions/3.2/Headers" \
        --global-option="-L/Library/Frameworks/GDAL.framework/Versions/3.2/unix/lib" \
        GDAL'<3.3'

Downscale [full-resolution HRSL data](https://registry.opendata.aws/dataforgood-fb-hrsl/) to 0.001 degrees precision:

    gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
        -tr 0.001 -0.001 -r sum -te -180 -90 180 90 \
        /vsis3/dataforgood-fb-data/hrsl-cogs/hrsl_general/hrsl_general-latest.vrt /tmp/world-3.tif

Further downscale to 0.01 degrees precision:

    gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
        -tr 0.01 -0.01 -r sum -te -180 -90 180 90 \
        /tmp/world-3.tif /tmp/world-2.tif

Further downscale to 0.1 degrees precision:

    gdalwarp -of GTIFF -co COMPRESS=LZW -co TILED=YES -ot Float32 \
        -tr 0.1 -0.1 -r sum -te -180 -90 180 90 \
        /tmp/world-2.tif /tmp/world-1.tif
