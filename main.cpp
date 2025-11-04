#include <bits/stdc++.h>
using namespace std;

/*
Problem 015 - File Storage (ACMOJ 2545)

Optimized design:
- Persist data in 16 bucket files under data/ directory: data/bk_0.dat ... data/bk_15.dat
- Each bucket file stores lines: <index>\t<count>\t<ascending values>\n
Performance optimization:
- Maintain a single in-memory cache for the currently active bucket only.
- On first access to a bucket in this run, load the entire bucket file into an unordered_map<string, vector<int>>.
- Apply operations to this map in O(log n) per index via lower_bound on the vector.
- Flush the bucket map back to disk only when switching to another bucket or at program end.
- This avoids scanning and rewriting the bucket file on every operation, dramatically reducing disk I/O.

Memory:
- Only one bucket is loaded at a time; with 16 buckets and total entries <= 100k, per-bucket memory stays within 5–6 MiB.

*/

static const int NUM_BUCKETS = 16; // keep under 20 files limit
static const string DATA_DIR = "data";

static string bucket_path(int b) {
    return DATA_DIR + "/bk_" + to_string(b) + ".dat";
}

static int bucket_id(const string &key) {
    size_t h = std::hash<string>{}(key);
    return (int)(h % NUM_BUCKETS);
}

struct BucketLine {
    string index;
    vector<int> values; // sorted ascending, unique
};

// Parse a bucket line: index\tcount\tval1 val2 ...
static bool parse_line(const string &line, BucketLine &out) {
    size_t p1 = line.find('\t');
    if (p1 == string::npos) return false;
    size_t p2 = line.find('\t', p1 + 1);
    if (p2 == string::npos) return false;
    out.index = line.substr(0, p1);
    string vals = line.substr(p2 + 1);
    out.values.clear();
    if (!vals.empty()) {
        stringstream ss(vals);
        int x;
        while (ss >> x) out.values.push_back(x);
    }
    // Ensure sorted unique
    sort(out.values.begin(), out.values.end());
    out.values.erase(unique(out.values.begin(), out.values.end()), out.values.end());
    return true;
}

static string serialize_line(const BucketLine &bl) {
    string s;
    s.reserve(bl.index.size() + bl.values.size() * 12);
    s += bl.index;
    s += '\t';
    s += to_string((int)bl.values.size());
    s += '\t';
    for (size_t i = 0; i < bl.values.size(); ++i) {
        if (i) s += ' ';
        s += to_string(bl.values[i]);
    }
    return s;
}

// Single-bucket cache
static int curr_bucket = -1;
static unordered_map<string, vector<int>> bucket_map;
static bool bucket_dirty = false;

static void load_bucket(int b) {
    bucket_map.clear();
    string path = bucket_path(b);
    ifstream fin(path);
    string line;
    while (fin.good() && getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        BucketLine bl;
        if (!parse_line(line, bl)) continue;
        bucket_map.emplace(std::move(bl.index), std::move(bl.values));
    }
    bucket_dirty = false;
}

// Write bucket_map back to file (atomic via temp file)
static void flush_bucket(int b) {
    if (!bucket_dirty) return;
    vector<string> lines;
    lines.reserve(bucket_map.size());
    for (const auto &kv : bucket_map) {
        BucketLine bl{kv.first, kv.second};
        lines.push_back(serialize_line(bl));
    }
    // Ensure directory exists
    filesystem::create_directories(DATA_DIR);
    string path = bucket_path(b);
    string tmp = path + ".tmp";
    {
        ofstream fout(tmp);
        for (const auto &s : lines) fout << s << '\n';
        fout.flush();
    }
    std::error_code ec;
    filesystem::rename(tmp, path, ec);
    if (ec) {
        filesystem::remove(path);
        filesystem::rename(tmp, path);
    }
    bucket_dirty = false;
}

static void ensure_bucket(int b) {
    if (curr_bucket == b) return;
    if (curr_bucket != -1) flush_bucket(curr_bucket);
    curr_bucket = b;
    load_bucket(b);
}

static void cmd_insert(const string &idx, int val) {
    int b = bucket_id(idx);
    ensure_bucket(b);
    auto &vec = bucket_map[idx]; // creates empty if not exists
    auto it = lower_bound(vec.begin(), vec.end(), val);
    if (it == vec.end() || *it != val) {
        vec.insert(it, val);
        bucket_dirty = true;
    }
}

static void cmd_delete(const string &idx, int val) {
    int b = bucket_id(idx);
    ensure_bucket(b);
    auto itIdx = bucket_map.find(idx);
    if (itIdx == bucket_map.end()) return;
    auto &vec = itIdx->second;
    auto it = lower_bound(vec.begin(), vec.end(), val);
    if (it != vec.end() && *it == val) {
        vec.erase(it);
        bucket_dirty = true;
    }
}

static void cmd_find(const string &idx) {
    int b = bucket_id(idx);
    ensure_bucket(b);
    auto itIdx = bucket_map.find(idx);
    if (itIdx == bucket_map.end() || itIdx->second.empty()) {
        cout << "null\n";
        return;
    }
    const auto &vec = itIdx->second;
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i) cout << ' ';
        cout << vec[i];
    }
    cout << '\n';
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Ensure data directory exists
    filesystem::create_directories(DATA_DIR);

    int n;
    if (!(cin >> n)) return 0;
    for (int i = 0; i < n; ++i) {
        string cmd, idx;
        cin >> cmd >> idx;
        if (cmd == "insert") {
            int val; cin >> val;
            cmd_insert(idx, val);
        } else if (cmd == "delete") {
            int val; cin >> val;
            cmd_delete(idx, val);
        } else if (cmd == "find") {
            cmd_find(idx);
        } else {
            // invalid command (shouldn't happen per spec)
        }
    }
    if (curr_bucket != -1) flush_bucket(curr_bucket);
    return 0;
}
