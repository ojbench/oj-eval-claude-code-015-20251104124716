#include <bits/stdc++.h>
using namespace std;

/*
Problem 015 - File Storage (ACMOJ 2545)

Optimized design v3:
- 20 bucket files under data/ directory: data/bk_0.dat ... data/bk_19.dat
- Small LRU cache of bucket contents (capacity = 6) to balance time vs memory
- Binary on-disk format for fast load/flush
  Header: 'BK1\0' (4 bytes)
  Repeated records: [u8 key_len][key bytes][u32 count][count * i32 values]
  Values are sorted ascending and unique.
- Fallback: if header missing, read legacy text format (index\tcount\tvals) then rewrite as binary on next flush.

Memory: at most 6 buckets cached concurrently to respect ~6 MiB limit.
*/

static const int NUM_BUCKETS = 20; // stay within 20-file limit
static const int BUCKET_CACHE_CAP = NUM_BUCKETS; // cache all buckets to avoid evictions
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

// Fallback text parser: index\tcount\tval1 val2 ... (values are already sorted unique)
static bool parse_line_fast(const string &line, string &index_out, vector<int> &vals_out) {
    size_t p1 = line.find('\t');
    if (p1 == string::npos) return false;
    size_t p2 = line.find('\t', p1 + 1);
    if (p2 == string::npos) return false;
    index_out.assign(line.data(), p1);
    vals_out.clear();
    const char *s = line.c_str() + p2 + 1;
    char *endptr = nullptr;
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

// Binary load/flush
static bool load_bucket_binary_file(const string &path, Bucket &bk) {
    ifstream fin(path, ios::binary);
    if (!fin.good()) return true; // empty is ok
    char hdr[4];
    if (!fin.read(hdr, 4)) return false;
    if (!(hdr[0]=='B' && hdr[1]=='K' && hdr[2]=='1' && hdr[3]=='\0')) return false;
    bk.map.reserve(1024);
    while (true) {
        unsigned char klen = 0;
        if (!fin.read(reinterpret_cast<char*>(&klen), 1)) break; // EOF
        string key;
        key.resize(klen);
        if (klen && !fin.read(&key[0], klen)) return false;
        uint32_t cnt = 0;
        if (!fin.read(reinterpret_cast<char*>(&cnt), 4)) return false;
        vector<int> vals(cnt);
        if (cnt) {
            if (!fin.read(reinterpret_cast<char*>(vals.data()), cnt * sizeof(int))) return false;
        }
        bk.map.emplace(std::move(key), std::move(vals));
    }
    return true;
}

static bool load_bucket_text_file(const string &path, Bucket &bk) {
    ifstream fin(path);
    if (!fin.good()) return true; // treat as empty
    string line, idx;
    vector<int> vals;
    bk.map.reserve(1024);
    while (getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!parse_line_fast(line, idx, vals)) continue;
        bk.map.emplace(idx, vals);
    }
    return true;
}

static void flush_bucket_binary_file(const string &path, const Bucket &bk) {
    filesystem::create_directories(DATA_DIR);
    string tmp = path + ".tmp";
    {
        ofstream fout(tmp, ios::binary);
        const char hdr[4] = {'B','K','1','\0'};
        fout.write(hdr, 4);
        for (const auto &kv : bk.map) {
            const string &key = kv.first;
            const vector<int> &vals = kv.second;
            unsigned char klen = (unsigned char)(key.size() & 0xFF);
            fout.write(reinterpret_cast<const char*>(&klen), 1);
            if (klen) fout.write(key.data(), klen);
            uint32_t cnt = (uint32_t)vals.size();
            fout.write(reinterpret_cast<const char*>(&cnt), 4);
            if (cnt) fout.write(reinterpret_cast<const char*>(vals.data()), cnt * sizeof(int));
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

static void flush_bucket_to_disk(int b, const Bucket &bk) {
    if (!bk.dirty) return;
    string path = bucket_path(b);
    flush_bucket_binary_file(path, bk);
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
    Bucket bk;
    string path = bucket_path(b);
    bool ok = load_bucket_binary_file(path, bk);
    if (!ok) {
        bk.map.clear();
        load_bucket_text_file(path, bk);
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
