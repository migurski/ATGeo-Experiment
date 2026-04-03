#include <aws/lambda-runtime/runtime.h>
#include <aws/core/Aws.h>
#include <aws/core/utils/json/JsonSerializer.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace aws::lambda_runtime;
using Aws::Utils::Json::JsonValue;

static int decimal_precision(const std::string& s) {
    auto dot = s.find('.');
    if (dot == std::string::npos) return 0;
    return static_cast<int>(s.size() - dot - 1);
}

invocation_response handler(invocation_request const& request) {
    try {
        JsonValue event(request.payload);
        if (!event.WasParseSuccessful())
            return invocation_response::failure("Failed to parse event JSON", "JsonParseError");

        auto view = event.View();
        auto qsp = view.GetObject("queryStringParameters");

        std::string lon_str, lat_str;
        if (qsp.IsObject()) {
            if (qsp.KeyExists("lon")) lon_str = qsp.GetString("lon").c_str();
            if (qsp.KeyExists("lat")) lat_str = qsp.GetString("lat").c_str();
        }

        if (lon_str.empty() || lat_str.empty()) {
            JsonValue resp;
            resp.WithInteger("statusCode", 400);
            resp.WithString("body", "Missing lon or lat\n");
            return invocation_response::success(resp.View().WriteCompact(), "application/json");
        }

        int prec_lon = decimal_precision(lon_str);
        int prec_lat = decimal_precision(lat_str);
        int precision = prec_lon < prec_lat ? prec_lon : prec_lat;

        if (precision >= 4) {
            JsonValue resp;
            resp.WithInteger("statusCode", 400);
            resp.WithString("body", "ValueError: precision too high (max 3 digits)\n");
            return invocation_response::success(resp.View().WriteCompact(), "application/json");
        }

        double lon = std::stod(lon_str);
        double lat = std::stod(lat_str);

        const char* bucket_env = std::getenv("DATA_BUCKET_NAME");
        if (!bucket_env)
            return invocation_response::failure("DATA_BUCKET_NAME not set", "ConfigError");
        std::string bucket(bucket_env);

        std::string tif;
        double step, half;
        if (precision == 0) {
            tif = "hrsl-1.tif"; step = 0.1;   half = 0.5;
        } else if (precision == 1) {
            tif = "hrsl-2.tif"; step = 0.01;  half = 0.05;
        } else if (precision == 2) {
            tif = "hrsl-3.tif"; step = 0.001; half = 0.005;
        } else {
            tif = "hrsl-3.tif"; step = 0.001; half = 0.0005;
        }

        // Round coordinate to detected precision, then build extent
        double factor = std::pow(10.0, precision);
        double lon_c = std::round(lon * factor) / factor;
        double lat_c = std::round(lat * factor) / factor;
        double xmin = lon_c - half;
        double xmax = lon_c + half;
        double ymin = lat_c - half;
        double ymax = lat_c + half;

        std::string path = "/vsis3/" + bucket + "/" + tif;
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
        if (!ds)
            return invocation_response::failure("GDALOpen failed for " + path, "GDALError");

        double gt[6];
        ds->GetGeoTransform(gt);
        // gt: [ulx, dx, 0, uly, 0, dy]  (dy is negative)

        int xoff  = static_cast<int>(std::round((xmin - gt[0]) / gt[1]));
        int yoff  = static_cast<int>(std::round((ymax - gt[3]) / gt[5]));
        int xsize = static_cast<int>(std::round((xmax - xmin) / gt[1]));
        int ysize = static_cast<int>(std::round((ymin - ymax) / gt[5]));

        std::vector<float> buf(xsize * ysize);
        CPLErr err = ds->GetRasterBand(1)->RasterIO(
            GF_Read, xoff, yoff, xsize, ysize,
            buf.data(), xsize, ysize, GDT_Float32, 0, 0);
        GDALClose(ds);

        if (err != CE_None)
            return invocation_response::failure("RasterIO failed", "GDALError");

        // Compute total (sum of non-NaN values)
        double total = 0.0;
        for (float v : buf) {
            if (!std::isnan(v)) total += v;
        }
        // Round total to 1 decimal place
        total = std::round(total * 10.0) / 10.0;

        // Build data matrix as JSON array-of-arrays
        int out_prec = precision + 1;
        double prec_factor = std::pow(10.0, out_prec);

        // Build body JSON manually for the nested array
        std::ostringstream body;
        body << "{";
        // ulx / uly rounded to out_prec decimal places, written with fixed
        // precision so default stream sig-figs don't truncate trailing digits
        auto fmt_coord = [&](double v, int p) -> std::string {
            double f = std::pow(10.0, p);
            double rounded = std::round(v * f) / f;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.*f", p, rounded);
            return buf;
        };
        body << "\"ulx\":" << fmt_coord(xmin, out_prec) << ",";
        body << "\"uly\":" << fmt_coord(ymax, out_prec) << ",";
        body << "\"dx\":" << step << ",";
        body << "\"dy\":" << (-step) << ",";
        body << "\"total\":" << total << ",";
        body << "\"data\":[";
        for (int row = 0; row < ysize; ++row) {
            if (row > 0) body << ",";
            body << "[";
            for (int col = 0; col < xsize; ++col) {
                if (col > 0) body << ",";
                float v = buf[row * xsize + col];
                if (std::isnan(v)) {
                    body << "null";
                } else {
                    double rv = std::round(static_cast<double>(v) * 10.0) / 10.0;
                    body << rv;
                }
            }
            body << "]";
        }
        body << "]}";

        JsonValue lambda_resp;
        lambda_resp.WithInteger("statusCode", 200);
        JsonValue headers;
        headers.WithString("Content-Type", "application/json");
        lambda_resp.WithObject("headers", headers);
        lambda_resp.WithString("body", body.str() + "\n");

        return invocation_response::success(
            lambda_resp.View().WriteCompact(), "application/json");

    } catch (const std::exception& e) {
        return invocation_response::failure(e.what(), "HandlerError");
    }
}

int main() {
    GDALAllRegister();

    Aws::SDKOptions options;
    Aws::InitAPI(options);

    auto handler_fn = [](invocation_request const& req) {
        return handler(req);
    };
    run_handler(handler_fn);

    Aws::ShutdownAPI(options);
    return 0;
}
