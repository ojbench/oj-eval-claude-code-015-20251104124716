#include <bits/stdc++.h>
using namespace std;

/*
Problem 015 - File Storage (ACMOJ 2545)

Requirements summary:
- Support commands: insert index value, delete index value, find index
- Persist data across runs using files
- Memory limit ~5-6 MiB, total entries <= 100k
- Output values for an index in ascending order
- No duplicate (index, value)
- Binary must be named 'code'

Design:
- Use a single append-only data file entries.dat storing lines: index\tvalue\n
- Maintain an on-disk index file index.idx mapping index -> sorted list of values.
  To keep memory usage low, we load only the target index's list when needed.

- Implementation approach:
  * Data files:
    - entries.dat : not strictly required for operation, but kept for simplicity (append-only log)
    - index.idx   : plaintext format with one line per index: index\tcount\tval1 val2 ... valN (ascending)

  * On startup: if index.idx doesn't exist, create empty. We DO NOT rebuild from entries.dat to save time; we trust persistence.

  * Operations:
    - insert i v: Load the line for index i (parse values), insert v if not present (keep sorted), write back the single line.
    - delete i v: Load line for i, erase v if present, write back.
    - find i: Load line for i, print values ascending, or null if none.

- File count: 2 files -> within limit; each op touches only its index line.
- Memory: Only the vector<int> for one index is held at a time.

Storage format details for index.idx:
- Each line: <index>\t<k>\t<space-separated ascending values>\n
- Example: CppPrimer\t2\t2001 2012\n
Implementation notes:
- Because we need random access to lines by index, we'll store lines in a simple unordered_map index->offset file to accelerate lookup. But maintaining a separate offset map complicates persistence.
- Simpler approach: maintain the entire index.idx in memory map at startup would exceed memory if many indices/values.
- Therefore, we use a per-index file under a directory would violate file-count limits in worst-case.

Compromise approach:
- Use a small hash table of index->offset persisted in idx_meta.bin (binary). We rewrite the specific line in index.idx by regenerating the whole file when a new key appears is expensive.
- Alternative: Use SQLite? Not allowed.

Simpler robust solution under limits:
- Use one file per bucket with limited number of buckets (e.g., 16 buckets), staying under 20 file limit. Each bucket stores multiple indices.
- File count: 1 (program) + 16 (buckets) + optional 1 log <= 18 -> within limit.

Bucket scheme:
- buckets/bk_XX.dat (XX=0..15). Compute hash of index and choose bucket.
- In each bucket file, store lines: index\tcount\tvalues\n. For operations on an index, we scan the bucket file to find line for index, modify, and rewrite the bucket file.
- Scan cost is proportional to number of distinct indices in the bucket; with up to 100k total entries and typical distribution, and time limit up to 16s, acceptable.
- Memory stays small: we read the bucket file into memory vector<string> for the bucket for the duration of the op.

Persistence across runs: bucket files remain on disk.

Edge cases:
- Duplicate insert ignored.
- Delete non-existent ignored.
- find prints 'null' if line missing or count==0.

Implementation details:
- Hash function: std::hash<string> % 16.
- Bucket directory name: "data". We'll create it if missing.
- Bucket filenames: data/bk_0.dat ... data/bk_15.dat
- We keep number of files: 16 bucket files + maybe directory -> counts as one? The limit is 20 files; directories not counted as files by typical limit, but the problem states file count limit includes files only. We are safe.

- File IO: For each op, we read entire bucket file (lines) into vector, locate index, parse values, modify, then write back the entire bucket file. This ensures consistency and simplicity.

Performance considerations:
- Worst-case if all indices hashed to same bucket, scanning file of O(#indices). With 100k entries and many indices, file may be large, but still manageable under 16s.
- Values per index are kept sorted; we'll maintain sorted order on insert by lower_bound and unique check.

Formatting:
- find output: values separated by single space, no extra spaces, newline at end. If empty: print "null\n".

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
    // count = stoi(line.substr(p1+1, p2-(p1+1))) but we don't trust, we parse values and ignore count
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

// Read bucket file into lines
static void read_bucket(int b, vector<string> &lines) {
    lines.clear();
    string path = bucket_path(b);
    ifstream fin(path);
    if (!fin.good()) return; // missing -> empty
    string line;
    while (getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // handle CRLF
        lines.push_back(line);
    }
}

// Write bucket lines back to file (atomic via temp file)
static void write_bucket(int b, const vector<string> &lines) {
    string path = bucket_path(b);
    // Ensure directory exists
    filesystem::create_directories(DATA_DIR);
    string tmp = path + ".tmp";
    {
        ofstream fout(tmp);
        for (size_t i = 0; i < lines.size(); ++i) {
            fout << lines[i] << '\n';
        }
        fout.flush();
    }
    // Replace
    std::error_code ec;
    filesystem::rename(tmp, path, ec);
    if (ec) {
        // Fallback: remove original and rename again
        filesystem::remove(path);
        filesystem::rename(tmp, path);
    }
}

// Find index line position in lines, returning -1 if not found
static int find_index_line(const vector<string> &lines, const string &idx) {
    for (size_t i = 0; i < lines.size(); ++i) {
        BucketLine bl;
        if (!parse_line(lines[i], bl)) continue;
        if (bl.index == idx) return (int)i;
    }
    return -1;
}

static void cmd_insert(const string &idx, int val) {
    int b = bucket_id(idx);
    vector<string> lines;
    read_bucket(b, lines);
    int pos = find_index_line(lines, idx);
    BucketLine bl;
    if (pos >= 0) {
        parse_line(lines[pos], bl);
    } else {
        bl.index = idx;
        bl.values.clear();
    }
    auto it = lower_bound(bl.values.begin(), bl.values.end(), val);
    if (it == bl.values.end() || *it != val) {
        bl.values.insert(it, val);
        string ser = serialize_line(bl);
        if (pos >= 0) lines[pos] = ser; else lines.push_back(ser);
        write_bucket(b, lines);
    } else {
        // duplicate, ignore; no write needed
    }
}

static void cmd_delete(const string &idx, int val) {
    int b = bucket_id(idx);
    vector<string> lines;
    read_bucket(b, lines);
    int pos = find_index_line(lines, idx);
    if (pos < 0) {
        // nothing to delete
        return;
    }
    BucketLine bl;
    parse_line(lines[pos], bl);
    auto it = lower_bound(bl.values.begin(), bl.values.end(), val);
    if (it != bl.values.end() && *it == val) {
        bl.values.erase(it);
        string ser = serialize_line(bl);
        lines[pos] = ser;
        write_bucket(b, lines);
    } else {
        // not found, ignore
    }
}

static void cmd_find(const string &idx) {
    int b = bucket_id(idx);
    vector<string> lines;
    read_bucket(b, lines);
    int pos = find_index_line(lines, idx);
    if (pos < 0) {
        cout << "null\n";
        return;
    }
    BucketLine bl;
    parse_line(lines[pos], bl);
    if (bl.values.empty()) {
        cout << "null\n";
        return;
    }
    for (size_t i = 0; i < bl.values.size(); ++i) {
        if (i) cout << ' ';
        cout << bl.values[i];
    }
    cout << '\n';
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Ensure data directory and bucket files exist
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
    return 0;
}
