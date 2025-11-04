#include <bits/stdc++.h>
using namespace std;

/*
Problem 015 - File Storage (ACMOJ 2545)

Optimized design v2:
- Persist data in 16 bucket files under data/ directory: data/bk_0.dat ... data/bk_15.dat
- Each bucket file stores lines: <index>\t<count>\t<ascending values>\n
Performance optimizations:
- Maintain a small LRU cache of bucket contents in memory (capacity = 2) to reduce load/flush thrashing
  when operations alternate between buckets.
- Parse without sort/unique since we always write sorted-unique lists.
- Flush a bucket only on eviction or at program end.

Memory:
- Only up to 2 buckets are loaded concurrently to respect the ~6 MiB limit.
*/

static const int NUM_BUCKETS = 16; // keep under 20 files limit
static const int BUCKET_CACHE_CAP = 2; // LRU capacity
static const string DATA_DIR = "data";

static string bucket_path(int b) {
    return DATA_DIR + "/bk_" + to_string(b) + ".dat";
}

static int bucket_id(const string &key) {
    size_t h = std::hash<string>{}(key);
    return (int)(h % NUM_BUCKETS);
}

struct Bucket {
    // index -> sorted unique values
    unordered_map<string, vector<int>> map;
    bool dirty = false;
};

// Parse a bucket line: index\tcount\tval1 val2 ... (values are already sorted unique)
static bool parse_line_fast(const string &line, string &index_out, vector<int> &vals_out) {
    size_t p1 = line.find('\t');
    if (p1 == string::npos) return false;
    size_t p2 = line.find('\t', p1 + 1);
    if (p2 == string::npos) return false;
    index_out.assign(line.data(), p1);
    // skip count: line.substr(p1+1, p2-(p1+1))
    vals_out.clear();
    // parse values from p2+1 to end
    const char *s = line.c_str() + p2 + 1;
    char *endptr = nullptr;
    // Use strtol-like parsing for speed
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        long v = strtol(s, &endptr, 10);
        if (s == endptr) break;
        vals_out.push_back((int)v);
        s = endptr;
    }
    return true;
}

static string serialize_line(const string &idx, const vector<int> &vals) {
    string s;
    // rough reserve: index + count(+tab) + values
    s.reserve(idx.size() + 16 + vals.size() * 12);
    s += idx;
    s += '\t';
    s += to_string((int)vals.size());
    s += '\t';
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) s += ' ';
        s += to_string(vals[i]);
    }
    return s;
}

// LRU cache: bucket id -> Bucket
static unordered_map<int, Bucket> cache;
static list<int> lru; // front = most recent, back = least recent
static unordered_map<int, list<int>::iterator> where;

static void touch_lru(int b) {
    auto it = where.find(b);
    if (it != where.end()) {
        lru.erase(it->second);
        lru.push_front(b);
        it->second = lru.begin();
    } else {
        lru.push_front(b);
        where[b] = lru.begin();
    }
}

static void flush_bucket_to_disk(int b, const Bucket &bk) {
    if (!bk.dirty) return;
    filesystem::create_directories(DATA_DIR);
    string path = bucket_path(b);
    string tmp = path + ".tmp";
    {
        ofstream fout(tmp);
        // Serialize entries; order doesn't matter
        for (const auto &kv : bk.map) {
            fout << serialize_line(kv.first, kv.second) << '\n';
        }
        fout.flush();
    }
    std::error_code ec;
    filesystem::rename(tmp, path, ec);
    if (ec) {
        filesystem::remove(path);
        filesystem::rename(tmp, path);
    }
}

static void evict_if_needed() {
    if ((int)cache.size() < BUCKET_CACHE_CAP) return;
    // Evict least recently used (back)
    int victim = lru.back();
    lru.pop_back();
    where.erase(victim);
    auto it = cache.find(victim);
    if (it != cache.end()) {
        flush_bucket_to_disk(victim, it->second);
        cache.erase(it);
    }
}

static Bucket &load_bucket(int b) {
    auto it = cache.find(b);
    if (it != cache.end()) {
        touch_lru(b);
        return it->second;
    }
    evict_if_needed();
    // Load from disk
    Bucket bk;
    string path = bucket_path(b);
    ifstream fin(path);
    string line;
    string idx;
    vector<int> vals;
    while (fin.good() && getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!parse_line_fast(line, idx, vals)) continue;
        bk.map.emplace(idx, vals);
    }
    bk.dirty = false;
    auto [insIt, _] = cache.emplace(b, std::move(bk));
    touch_lru(b);
    return insIt->second;
}

static void cmd_insert(const string &idx, int val) {
    int b = bucket_id(idx);
    Bucket &bk = load_bucket(b);
    auto &vec = bk.map[idx]; // creates empty if not exists
    auto it = lower_bound(vec.begin(), vec.end(), val);
    if (it == vec.end() || *it != val) {
        vec.insert(it, val);
        bk.dirty = true;
    }
}

static void cmd_delete(const string &idx, int val) {
    int b = bucket_id(idx);
    Bucket &bk = load_bucket(b);
    auto itIdx = bk.map.find(idx);
    if (itIdx == bk.map.end()) return;
    auto &vec = itIdx->second;
    auto it = lower_bound(vec.begin(), vec.end(), val);
    if (it != vec.end() && *it == val) {
        vec.erase(it);
        bk.dirty = true;
    }
}

static void cmd_find(const string &idx) {
    int b = bucket_id(idx);
    Bucket &bk = load_bucket(b);
    auto itIdx = bk.map.find(idx);
    if (itIdx == bk.map.end() || itIdx->second.empty()) {
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
            // invalid command
        }
    }
    // Flush all cached buckets
    for (const auto &b : lru) {
        auto it = cache.find(b);
        if (it != cache.end()) flush_bucket_to_disk(b, it->second);
    }
    return 0;
}
