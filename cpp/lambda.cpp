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
static const double MERCATOR_HALF = 20037508.34;

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

static std::pair<long long, long long> quadkey_tile_xy(const std::string& key) {
    // Replicates _quadkey_tile_xy from lambda.py:
    // decompose each base-4 digit to 2 bits, split into ybin/xbin, parse as integers
    std::string xbin, ybin;
    for (char c : key) {
        int v = c - '0';
        ybin += ((v >> 1) & 1) ? '1' : '0';
        xbin += ((v >> 0) & 1) ? '1' : '0';
    }
    long long xtile = 0, ytile = 0;
    for (char b : xbin) xtile = xtile * 2 + (b - '0');
    for (char b : ybin) ytile = ytile * 2 + (b - '0');
    return { xtile, ytile };
}

static invocation_response quadkey_handler(
        const std::string& qk, const std::string& geotiff_dir) {

    auto error400 = [](const std::string& msg) {
        JsonValue resp;
        resp.WithInteger("statusCode", 400);
        resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
        resp.WithString("body", msg);
        return invocation_response::success(resp.View().WriteCompact(), "application/json");
    };

    if (qk.empty())
        return error400("Missing quadkey\n");
    if (qk.size() > 18)
        return error400("ValueError: quadkey too long (max 18 characters)\n");
    for (char c : qk) {
        if (c < '0' || c > '3')
            return error400("ValueError: invalid quadkey character\n");
    }

    // Parent cell upper-left in EPSG:3857
    auto [px, py] = quadkey_tile_xy(qk);
    double parent_pixel_size = 2.0 * MERCATOR_HALF / static_cast<double>(1LL << qk.size());
    double ulx = -MERCATOR_HALF + px * parent_pixel_size;
    double uly =  MERCATOR_HALF - py * parent_pixel_size;

    std::vector<std::pair<std::string, double>> results;
    double total = 0.0;
    double dx, dy;

    if (qk.size() == 18) {
        std::string path = geotiff_dir + "/quadkey-18char.tif";
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
        if (!ds)
            return invocation_response::failure("GDALOpen failed for " + path, "GDALError");
        float val;
        CPLErr err = ds->GetRasterBand(1)->RasterIO(
            GF_Read, static_cast<int>(px), static_cast<int>(py), 1, 1,
            &val, 1, 1, GDT_Float32, 0, 0);
        GDALClose(ds);
        if (err != CE_None)
            return invocation_response::failure("RasterIO failed", "GDALError");
        double count = std::isnan(val) ? 0.0 : std::round(static_cast<double>(val) * 10.0) / 10.0;
        total = count;
        results.push_back({ qk, count });
        dx = parent_pixel_size;
        dy = -parent_pixel_size;
    } else {
        int max_depth = static_cast<int>(qk.size()) + 3;
        if (max_depth > 18) max_depth = 18;

        double finest_pixel_size = 2.0 * MERCATOR_HALF / static_cast<double>(1LL << max_depth);
        dx = finest_pixel_size;
        dy = -finest_pixel_size;

        // Read all finest-level children from one TIFF, then sum groups of 4
        // to derive intermediate levels — avoids opening multiple TIFFs.
        std::string tif_path = geotiff_dir + "/quadkey-" + std::to_string(max_depth) + "char.tif";
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(tif_path.c_str(), GA_ReadOnly));
        if (!ds)
            return invocation_response::failure("GDALOpen failed for " + tif_path, "GDALError");

        int finest_suffix_len = max_depth - static_cast<int>(qk.size());
        long long num_finest = 1LL << (2 * finest_suffix_len);  // 4^finest_suffix_len
        std::vector<std::pair<std::string, double>> finest;
        finest.reserve(num_finest);

        for (long long i = 0; i < num_finest; ++i) {
            std::string child = qk;
            child.resize(qk.size() + finest_suffix_len, '0');
            long long tmp = i;
            for (int j = finest_suffix_len - 1; j >= 0; --j) {
                child[qk.size() + j] = static_cast<char>('0' + (tmp % 4));
                tmp /= 4;
            }
            auto [cx, cy] = quadkey_tile_xy(child);
            float val;
            CPLErr err = ds->GetRasterBand(1)->RasterIO(
                GF_Read, static_cast<int>(cx), static_cast<int>(cy), 1, 1,
                &val, 1, 1, GDT_Float32, 0, 0);
            if (err != CE_None) {
                GDALClose(ds);
                return invocation_response::failure("RasterIO failed", "GDALError");
            }
            double count = std::isnan(val) ? 0.0 : std::round(static_cast<double>(val) * 10.0) / 10.0;
            total += count;
            finest.push_back({ child, count });
        }
        GDALClose(ds);

        // Build intermediate levels by summing groups of 4 from the finest level upward.
        // Process depths from max_depth down to qk.size()+1, populating results at each.
        // current_level holds counts at the depth being processed (starts at finest).
        std::vector<std::pair<std::string, double>> current_level = finest;
        for (int depth = max_depth; depth > static_cast<int>(qk.size()); --depth) {
            for (auto& [key, count] : current_level)
                results.push_back({ key, count });
            if (depth > static_cast<int>(qk.size()) + 1) {
                // Aggregate into parent level: sum each group of 4 siblings (unrounded)
                std::vector<std::pair<std::string, double>> parent_level;
                parent_level.reserve(current_level.size() / 4);
                for (size_t i = 0; i < current_level.size(); i += 4) {
                    std::string parent_key = current_level[i].first.substr(0, depth - 1);
                    double parent_count = 0.0;
                    for (size_t j = i; j < i + 4 && j < current_level.size(); ++j)
                        parent_count += current_level[j].second;
                    parent_level.push_back({ parent_key, parent_count });
                }
                current_level = std::move(parent_level);
            }
        }
    }

    total = std::round(total * 10.0) / 10.0;

    std::ostringstream body;
    body << "{\"quadkey\":\"" << qk << "\",";
    body << "\"ulx\":" << ulx << ",";
    body << "\"uly\":" << uly << ",";
    body << "\"dx\":" << dx << ",";
    body << "\"dy\":" << dy << ",";
    body << "\"total\":" << total << ",";
    body << "\"sub-areas\":{";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) body << ",";
        const std::string& key = results[i].first;
        double count = results[i].second;
        body << "\"" << key << "\":{\"link\":\"/dgg?quadkey=" << key << "\",\"count\":" << count << "}";
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

static std::string dd_body(const std::string& lon_str, const std::string& lat_str,
                            const std::string& geotiff_dir) {
    int prec_lon = decimal_precision(lon_str);
    int prec_lat = decimal_precision(lat_str);
    int precision = prec_lon < prec_lat ? prec_lon : prec_lat;

    if (precision >= 4) return "";

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

    double factor = std::pow(10.0, precision);
    double lon_c = std::round(lon * factor) / factor;
    double lat_c = std::round(lat * factor) / factor;
    double xmin = lon_c - half;
    double xmax = lon_c + half;
    double ymin = lat_c - half;
    double ymax = lat_c + half;

    std::string path = geotiff_dir + "/" + tif;
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds) return "";

    double gt[6];
    ds->GetGeoTransform(gt);

    int xoff  = static_cast<int>(std::round((xmin - gt[0]) / gt[1]));
    int yoff  = static_cast<int>(std::round((ymax - gt[3]) / gt[5]));
    int xsize = static_cast<int>(std::round((xmax - xmin) / gt[1]));
    int ysize = static_cast<int>(std::round((ymin - ymax) / gt[5]));

    std::vector<float> buf(xsize * ysize);
    CPLErr err = ds->GetRasterBand(1)->RasterIO(
        GF_Read, xoff, yoff, xsize, ysize,
        buf.data(), xsize, ysize, GDT_Float32, 0, 0);
    GDALClose(ds);

    if (err != CE_None) return "";

    double total = 0.0;
    for (float v : buf) {
        if (!std::isnan(v)) total += v;
    }
    total = std::round(total * 10.0) / 10.0;

    int out_prec = precision + 1;
    auto fmt_coord = [](double v, int p) -> std::string {
        double f = std::pow(10.0, p);
        double rounded = std::round(v * f) / f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.*f", p, rounded);
        return buf;
    };

    std::ostringstream body;
    body << "{";
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
    return body.str();
}

invocation_response handler(invocation_request const& request) {
    try {
        JsonValue event(request.payload);
        if (!event.WasParseSuccessful())
            return invocation_response::failure("Failed to parse event JSON", "JsonParseError");

        auto view = event.View();

        std::string raw_path;
        if (view.KeyExists("rawPath")) raw_path = view.GetString("rawPath").c_str();
        else if (view.KeyExists("path")) raw_path = view.GetString("path").c_str();

        const char* geotiff_dir_env = std::getenv("GEOTIFF_DIR");
        if (!geotiff_dir_env)
            return invocation_response::failure("GEOTIFF_DIR not set", "ConfigError");
        std::string geotiff_dir(geotiff_dir_env);

        if (raw_path == "/dgg") {
            auto qsp = view.GetObject("queryStringParameters");
            if (qsp.IsObject() && qsp.KeyExists("quadkey")) {
                std::string qk = qsp.GetString("quadkey").c_str();
                return quadkey_handler(qk, geotiff_dir);
            }
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

        int precision = std::min(decimal_precision(lon_str), decimal_precision(lat_str));
        if (precision >= 4) {
            JsonValue resp;
            resp.WithInteger("statusCode", 400);
            resp.WithObject("headers", JsonValue().WithString("Content-Type", "text/plain"));
            resp.WithString("body", "ValueError: precision too high (max 3 digits)\n");
            return invocation_response::success(resp.View().WriteCompact(), "application/json");
        }

        std::string body = dd_body(lon_str, lat_str, geotiff_dir);
        if (body.empty())
            return invocation_response::failure("dd_body failed", "GDALError");

        JsonValue lambda_resp;
        lambda_resp.WithInteger("statusCode", 200);
        JsonValue headers;
        headers.WithString("Content-Type", "application/json");
        lambda_resp.WithObject("headers", headers);
        lambda_resp.WithString("body", body + "\n");

        return invocation_response::success(
            lambda_resp.View().WriteCompact(), "application/json");

    } catch (const std::exception& e) {
        return invocation_response::failure(e.what(), "HandlerError");
    }
}

int main(int argc, char* argv[]) {
    GDALAllRegister();

    // CLI mode: --lonlat <lon> <lat>  or  --geohash <gh>  or  --quadkey <qk>
    if (argc > 1 && (std::string(argv[1]) == "--lonlat" || std::string(argv[1]) == "--geohash" || std::string(argv[1]) == "--quadkey")) {
        const char* geotiff_dir_env = std::getenv("GEOTIFF_DIR");
        if (!geotiff_dir_env) {
            std::fprintf(stderr, "GEOTIFF_DIR not set\n");
            return 1;
        }
        std::string geotiff_dir(geotiff_dir_env);

        std::string mode(argv[1]);
        if (mode == "--lonlat" && argc == 4) {
            std::string body = dd_body(argv[2], argv[3], geotiff_dir);
            if (body.empty()) {
                std::fprintf(stderr, "dd_body failed\n");
                return 1;
            }
            std::printf("%s\n", body.c_str());
        } else if (mode == "--geohash" && argc == 3) {
            // Reuse dgg_handler via a fake invocation_response — extract body directly
            Aws::SDKOptions options;
            Aws::InitAPI(options);
            auto resp = dgg_handler(argv[2], geotiff_dir);
            Aws::ShutdownAPI(options);
            if (!resp.is_success()) {
                std::fprintf(stderr, "dgg_handler failed: %s\n", resp.get_payload().c_str());
                return 1;
            }
            // resp payload is the Lambda JSON envelope; extract body field
            JsonValue envelope(resp.get_payload());
            std::string body = envelope.View().GetString("body").c_str();
            std::printf("%s", body.c_str());
        } else if (mode == "--quadkey" && argc == 3) {
            Aws::SDKOptions options;
            Aws::InitAPI(options);
            auto resp = quadkey_handler(argv[2], geotiff_dir);
            Aws::ShutdownAPI(options);
            if (!resp.is_success()) {
                std::fprintf(stderr, "quadkey_handler failed: %s\n", resp.get_payload().c_str());
                return 1;
            }
            JsonValue envelope(resp.get_payload());
            std::string body = envelope.View().GetString("body").c_str();
            std::printf("%s", body.c_str());
        } else {
            std::fprintf(stderr, "Usage: %s --lonlat <lon> <lat> | --geohash <geohash> | --quadkey <quadkey>\n", argv[0]);
            return 1;
        }
        return 0;
    }

    Aws::SDKOptions options;
    Aws::InitAPI(options);

    auto handler_fn = [](invocation_request const& req) {
        return handler(req);
    };
    run_handler(handler_fn);

    Aws::ShutdownAPI(options);
    return 0;
}
