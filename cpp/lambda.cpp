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

static const std::string GEOHASH_ALPHABET = "0123456789bcdefghjkmnpqrstuvwxyz";

struct BBox { double lon1, lat1, lon2, lat2; };

static BBox geohash2lonlats(const std::string& gh) {
    // Decode each character to 5 bits, interleave: even bit positions → lon (x), odd → lat (y)
    std::string bits;
    bits.reserve(gh.size() * 5);
    for (char c : gh) {
        int idx = static_cast<int>(GEOHASH_ALPHABET.find(c));
        for (int i = 4; i >= 0; --i)
            bits += ((idx >> i) & 1) ? '1' : '0';
    }
    std::string xbin, ybin;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (i % 2 == 0) xbin += bits[i];
        else             ybin += bits[i];
    }
    int xlen = static_cast<int>(xbin.size());
    int ylen = static_cast<int>(ybin.size());
    double xden = std::pow(2.0, xlen);
    double yden = std::pow(2.0, ylen);
    // Parse binary strings
    long long xval = 0, yval = 0;
    for (char c : xbin) xval = xval * 2 + (c - '0');
    for (char c : ybin) yval = yval * 2 + (c - '0');
    double x1 = xval / xden, x2 = (xval + 1) / xden;
    double y1 = yval / yden, y2 = (yval + 1) / yden;
    return { x1 * 360.0 - 180.0, y1 * 180.0 - 90.0,
             x2 * 360.0 - 180.0, y2 * 180.0 - 90.0 };
}

static invocation_response dgg_handler(
        const std::string& gh, const std::string& geotiff_dir) {

    // Validation
    if (gh.empty()) {
        JsonValue resp;
        resp.WithInteger("statusCode", 400);
        resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
        resp.WithString("body", "Missing geohash\n");
        return invocation_response::success(resp.View().WriteCompact(), "application/json");
    }
    if (gh.size() > 7) {
        JsonValue resp;
        resp.WithInteger("statusCode", 400);
        resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
        resp.WithString("body", "ValueError: geohash too long (max 7 characters)\n");
        return invocation_response::success(resp.View().WriteCompact(), "application/json");
    }
    for (char c : gh) {
        if (GEOHASH_ALPHABET.find(c) == std::string::npos) {
            JsonValue resp;
            resp.WithInteger("statusCode", 400);
            resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
            resp.WithString("body", "ValueError: invalid geohash character\n");
            return invocation_response::success(resp.View().WriteCompact(), "application/json");
        }
    }

    bool at_max_depth = (gh.size() == 7);
    std::string tif = at_max_depth
        ? "geohash-7char.tif"
        : "geohash-" + std::to_string(gh.size() + 1) + "char.tif";

    BBox pbb = geohash2lonlats(gh);
    // dx/dy: child cell dimensions (at max depth, the cell itself)
    BBox cbb = at_max_depth ? pbb : geohash2lonlats(gh + GEOHASH_ALPHABET[0]);
    double dx = cbb.lon2 - cbb.lon1;
    double dy = cbb.lat1 - cbb.lat2;  // negative

    std::string path = geotiff_dir + "/" + tif;
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds)
        return invocation_response::failure("GDALOpen failed for " + path, "GDALError");

    double gt[6];
    ds->GetGeoTransform(gt);

    // Build list of (key, bbox) pairs to read
    std::vector<std::pair<std::string, BBox>> cells;
    if (at_max_depth) {
        cells.push_back({ gh, geohash2lonlats(gh) });
    } else {
        for (char c : GEOHASH_ALPHABET) {
            std::string child = gh + c;
            cells.push_back({ child, geohash2lonlats(child) });
        }
    }

    // Read one pixel per cell
    std::vector<std::pair<std::string, double>> results;
    double total = 0.0;
    CPLErr err = CE_None;
    for (auto& [key, bb] : cells) {
        double lon_c = (bb.lon1 + bb.lon2) / 2.0;
        double lat_c = (bb.lat1 + bb.lat2) / 2.0;
        int xoff = static_cast<int>((lon_c - gt[0]) / gt[1]);
        int yoff = static_cast<int>((lat_c - gt[3]) / gt[5]);
        float val;
        err = ds->GetRasterBand(1)->RasterIO(
            GF_Read, xoff, yoff, 1, 1, &val, 1, 1, GDT_Float32, 0, 0);
        if (err != CE_None) break;
        double count = std::isnan(val) ? 0.0 : std::round(static_cast<double>(val) * 10.0) / 10.0;
        total += count;
        results.push_back({ key, count });
    }
    GDALClose(ds);

    if (err != CE_None)
        return invocation_response::failure("RasterIO failed", "GDALError");

    total = std::round(total * 10.0) / 10.0;

    // Build JSON body
    std::ostringstream body;
    body << "{\"geohash\":\"" << gh << "\",";
    body << "\"ulx\":" << pbb.lon1 << ",";
    body << "\"uly\":" << pbb.lat2 << ",";
    body << "\"dx\":" << dx << ",";
    body << "\"dy\":" << dy << ",";
    body << "\"total\":" << total << ",";
    body << "\"sub-areas\":{";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) body << ",";
        const std::string& key = results[i].first;
        double count = results[i].second;
        body << "\"" << key << "\":{\"link\":\"/dgg?geohash=" << key << "\",\"count\":" << count << "}";
    }
    body << "}}";

    JsonValue lambda_resp;
    lambda_resp.WithInteger("statusCode", 200);
    JsonValue headers;
    headers.WithString("Content-Type", "application/json");
    lambda_resp.WithObject("headers", headers);
    lambda_resp.WithString("body", body.str() + "\n");
    return invocation_response::success(lambda_resp.View().WriteCompact(), "application/json");
}

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

        // Route /dgg requests
        std::string raw_path;
        if (view.KeyExists("rawPath")) raw_path = view.GetString("rawPath").c_str();
        else if (view.KeyExists("path")) raw_path = view.GetString("path").c_str();

        const char* geotiff_dir_env = std::getenv("GEOTIFF_DIR");
        if (!geotiff_dir_env)
            return invocation_response::failure("GEOTIFF_DIR not set", "ConfigError");
        std::string geotiff_dir(geotiff_dir_env);

        if (raw_path == "/dgg") {
            auto qsp = view.GetObject("queryStringParameters");
            std::string gh;
            if (qsp.IsObject() && qsp.KeyExists("geohash"))
                gh = qsp.GetString("geohash").c_str();
            return dgg_handler(gh, geotiff_dir);
        }

        auto qsp = view.GetObject("queryStringParameters");

        std::string lon_str, lat_str;
        if (qsp.IsObject()) {
            if (qsp.KeyExists("lon")) lon_str = qsp.GetString("lon").c_str();
            if (qsp.KeyExists("lat")) lat_str = qsp.GetString("lat").c_str();
        }

        if (lon_str.empty() || lat_str.empty()) {
            JsonValue resp;
            resp.WithInteger("statusCode", 400);
            resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
            resp.WithString("body", "Missing lon or lat\n");
            return invocation_response::success(resp.View().WriteCompact(), "application/json");
        }

        int prec_lon = decimal_precision(lon_str);
        int prec_lat = decimal_precision(lat_str);
        int precision = prec_lon < prec_lat ? prec_lon : prec_lat;

        if (precision >= 4) {
            JsonValue resp;
            resp.WithInteger("statusCode", 400);
            resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
            resp.WithString("body", "ValueError: precision too high (max 3 digits)\n");
            return invocation_response::success(resp.View().WriteCompact(), "application/json");
        }

        double lon = std::stod(lon_str);
        double lat = std::stod(lat_str);

        std::string tif;
        double step, half;
        if (precision == 0) {
            tif = "degree-1digit.tif"; step = 0.1;   half = 0.5;
        } else if (precision == 1) {
            tif = "degree-2digit.tif"; step = 0.01;  half = 0.05;
        } else if (precision == 2) {
            tif = "degree-3digit.tif"; step = 0.001; half = 0.005;
        } else {
            tif = "degree-3digit.tif"; step = 0.001; half = 0.0005;
        }

        // Round coordinate to detected precision, then build extent
        double factor = std::pow(10.0, precision);
        double lon_c = std::round(lon * factor) / factor;
        double lat_c = std::round(lat * factor) / factor;
        double xmin = lon_c - half;
        double xmax = lon_c + half;
        double ymin = lat_c - half;
        double ymax = lat_c + half;

        std::string path = geotiff_dir + "/" + tif;
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
