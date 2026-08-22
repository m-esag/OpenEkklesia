#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include "database/databasemanager.h"
#include "ui/ministry/ministrymatch.h"

// --- Performance Benchmark Utilities ---

struct BenchmarkResult {
    std::string name;
    double totalTimeMs;
    int iterations;
    double timePerOpUs;
    std::string throughput;
    std::string status;
};

void printBenchmarkHeader(const std::string& suiteName) {
    std::cout << "\n"
              << "========================================================================================\n"
              << " ⚡ BENCHMARK: " << suiteName << "\n"
              << "========================================================================================\n"
              << std::left << std::setw(40) << "Benchmark Name"
              << std::right << std::setw(15) << "Iterations"
              << std::setw(15) << "Total Time"
              << std::setw(18) << "Avg / Op"
              << std::setw(20) << "Throughput"
              << "\n----------------------------------------------------------------------------------------\n";
}

void printResult(const BenchmarkResult& r) {
    std::cout << std::left << std::setw(40) << r.name
              << std::right << std::setw(15) << r.iterations
              << std::setw(12) << std::fixed << std::setprecision(2) << r.totalTimeMs << " ms"
              << std::setw(14) << std::fixed << std::setprecision(2) << r.timePerOpUs << " µs"
              << std::setw(20) << r.throughput
              << "\n";
}

// 1. Connection Graph Physics Benchmark
void benchmarkGraphPhysics() {
    printBenchmarkHeader("Connection Graph Force-Directed Physics Engine");

    const std::vector<int> nodeCounts = {100, 300, 600, 1000};
    for (int N : nodeCounts) {
        // Simulate N nodes with random positions
        struct SimpleNode { double x, y, dx, dy; };
        std::vector<SimpleNode> nodes;
        nodes.reserve(N);
        for (int i = 0; i < N; ++i) {
            SimpleNode sn;
            sn.x = static_cast<double>(QRandomGenerator::global()->bounded(1000) - 500);
            sn.y = static_cast<double>(QRandomGenerator::global()->bounded(1000) - 500);
            sn.dx = 0.0;
            sn.dy = 0.0;
            nodes.push_back(sn);
        }

        // Simulate random edges (average degree 3)
        struct SimpleEdge { int from, to; };
        std::vector<SimpleEdge> edges;
        for (int i = 0; i < N * 3; ++i) {
            int u = QRandomGenerator::global()->bounded(N);
            int v = QRandomGenerator::global()->bounded(N);
            if (u != v) edges.push_back({u, v});
        }

        const int frames = 120; // 2 seconds of 60 FPS animation
        QElapsedTimer timer;
        timer.start();

        const double repulsion = 12000.0;
        const double attraction = 0.003;
        const double damping = 0.75;
        const double maxSpeed = 5.0;

        for (int f = 0; f < frames; ++f) {
            // Repulsion O(N^2)
            for (int i = 0; i < N; ++i) {
                for (int j = i + 1; j < N; ++j) {
                    double dx = nodes[i].x - nodes[j].x;
                    double dy = nodes[i].y - nodes[j].y;
                    double distSq = dx * dx + dy * dy;
                    if (distSq < 2025.0) distSq = 2025.0; // 45px clamp
                    double dist = std::sqrt(distSq);
                    double force = repulsion / distSq;
                    double fx = (dx / dist) * force;
                    double fy = (dy / dist) * force;
                    nodes[i].dx += fx;
                    nodes[i].dy += fy;
                    nodes[j].dx -= fx;
                    nodes[j].dy -= fy;
                }
            }

            // Attraction O(E)
            for (const auto& e : edges) {
                double dx = nodes[e.to].x - nodes[e.from].x;
                double dy = nodes[e.to].y - nodes[e.from].y;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 1.0) continue;
                double force = attraction * (dist - 140.0);
                double fx = (dx / dist) * force;
                double fy = (dy / dist) * force;
                nodes[e.from].dx += fx;
                nodes[e.from].dy += fy;
                nodes[e.to].dx -= fx;
                nodes[e.to].dy -= fy;
            }

            // Integration & velocity clamp
            for (int i = 0; i < N; ++i) {
                nodes[i].dx *= damping;
                nodes[i].dy *= damping;
                double speed = std::sqrt(nodes[i].dx * nodes[i].dx + nodes[i].dy * nodes[i].dy);
                if (speed > maxSpeed) {
                    nodes[i].dx = (nodes[i].dx / speed) * maxSpeed;
                    nodes[i].dy = (nodes[i].dy / speed) * maxSpeed;
                }
                nodes[i].x += nodes[i].dx;
                nodes[i].y += nodes[i].dy;
            }
        }

        qint64 elapsedNs = timer.nsecsElapsed();
        double elapsedMs = elapsedNs / 1e6;
        double msPerFrame = elapsedMs / frames;
        double fps = 1000.0 / msPerFrame;

        BenchmarkResult r;
        r.name = "Graph Physics (" + std::to_string(N) + " nodes, " + std::to_string(edges.size()) + " edges)";
        r.iterations = frames;
        r.totalTimeMs = elapsedMs;
        r.timePerOpUs = msPerFrame * 1000.0;
        r.throughput = std::to_string(static_cast<int>(fps)) + " FPS (" + (fps >= 60.0 ? "⚡ 60+ FPS Ready" : "⚠️ Sub-60 FPS") + ")";
        printResult(r);
    }
}

// 2. Database Bulk Transactions Benchmark
void benchmarkDatabaseThroughput(QSqlDatabase& db) {
    printBenchmarkHeader("SQLite Database Throughput (WAL Mode)");

    // Benchmark 1: Bulk rapid contribution insert
    const int count = 5000;
    QElapsedTimer timer;
    timer.start();

    db.transaction();
    QSqlQuery q(db);
    q.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, note) VALUES (?, 1, ?, '2026-08-23', 'Batch Benchmark')");
    for (int i = 1; i <= count; ++i) {
        q.addBindValue(i % 100 + 1);
        q.addBindValue(QRandomGenerator::global()->bounded(1000, 50000));
        q.exec();
    }
    db.commit();

    double insertTimeMs = timer.nsecsElapsed() / 1e6;
    BenchmarkResult rInsert;
    rInsert.name = "Bulk Contributions Insert (5,000 rows)";
    rInsert.iterations = count;
    rInsert.totalTimeMs = insertTimeMs;
    rInsert.timePerOpUs = (insertTimeMs / count) * 1000.0;
    rInsert.throughput = std::to_string(static_cast<int>((count / (insertTimeMs / 1000.0)))) + " records/sec";
    printResult(rInsert);

    // Benchmark 2: 360° Dossier Complex Query
    const int queries = 1000;
    timer.restart();
    for (int i = 1; i <= queries; ++i) {
        int personId = (i % 100) + 1;
        QSqlQuery dq(db);
        dq.prepare("SELECT p.first_name, p.last_name, h.family_name, h.address, "
                   "       COALESCE(SUM(c.amount), 0) AS total_giving, "
                   "       COUNT(DISTINCT a.id) AS attendance_count "
                   "FROM people p "
                   "LEFT JOIN households h ON p.household_id = h.id "
                   "LEFT JOIN contributions c ON p.id = c.person_id "
                   "LEFT JOIN attendance a ON p.id = a.person_id "
                   "WHERE p.id = ? "
                   "GROUP BY p.id");
        dq.addBindValue(personId);
        dq.exec();
        if (dq.next()) {
            // Read columns
            volatile double amt = dq.value(4).toDouble();
            (void)amt;
        }
    }

    double queryTimeMs = timer.nsecsElapsed() / 1e6;
    BenchmarkResult rQuery;
    rQuery.name = "360° Member Dossier Aggregation Query";
    rQuery.iterations = queries;
    rQuery.totalTimeMs = queryTimeMs;
    rQuery.timePerOpUs = (queryTimeMs / queries) * 1000.0;
    rQuery.throughput = std::to_string(static_cast<int>((queries / (queryTimeMs / 1000.0)))) + " queries/sec";
    printResult(rQuery);

    // Benchmark 3: 90-Day Pastoral Inactivity Scan
    const int scanCount = 500;
    timer.restart();
    for (int i = 0; i < scanCount; ++i) {
        QSqlQuery sq(db);
        sq.exec("SELECT p.id, p.first_name || ' ' || p.last_name, MAX(a.attendance_date) as last_seen "
                "FROM people p "
                "LEFT JOIN attendance a ON p.id = a.person_id "
                "GROUP BY p.id "
                "HAVING last_seen < date('now', '-90 days') OR last_seen IS NULL");
        while (sq.next()) {
            volatile int pid = sq.value(0).toInt();
            (void)pid;
        }
    }
    double scanTimeMs = timer.nsecsElapsed() / 1e6;
    BenchmarkResult rScan;
    rScan.name = "Pastoral 90-Day Inactivity Scan";
    rScan.iterations = scanCount;
    rScan.totalTimeMs = scanTimeMs;
    rScan.timePerOpUs = (scanTimeMs / scanCount) * 1000.0;
    rScan.throughput = std::to_string(static_cast<int>((scanCount / (scanTimeMs / 1000.0)))) + " scans/sec";
    printResult(rScan);
}

// 3. Ministry Matching Algorithm Benchmark
void benchmarkMinistryMatch(QSqlDatabase& db) {
    printBenchmarkHeader("Ministry Match Assimilation & Scoring Algorithm");

    MatchCriteria criteria;
    criteria.maxResults = 25;

    const int iterations = 100;
    QElapsedTimer timer;
    timer.start();

    int totalCandidatesRanked = 0;
    for (int i = 0; i < iterations; ++i) {
        QList<MatchResult> results = MinistryRecommender::recommend(db, 1, criteria);
        totalCandidatesRanked += results.size();
    }

    double totalTimeMs = timer.nsecsElapsed() / 1e6;
    BenchmarkResult r;
    r.name = "Group Fit Scoring (MinistryRecommender::recommend)";
    r.iterations = iterations;
    r.totalTimeMs = totalTimeMs;
    r.timePerOpUs = (totalTimeMs / iterations) * 1000.0;
    r.throughput = std::to_string(static_cast<int>(totalCandidatesRanked / (totalTimeMs / 1000.0))) + " candidates/sec";
    printResult(r);

    // Role-based ranking
    timer.restart();
    int roleCandidatesRanked = 0;
    for (int i = 0; i < iterations; ++i) {
        QList<MatchResult> roleResults = MinistryRecommender::rankByRole(db, "Acoustic Guitar");
        roleCandidatesRanked += roleResults.size();
    }
    double roleTimeMs = timer.nsecsElapsed() / 1e6;
    BenchmarkResult rRole;
    rRole.name = "Role Fit Ranking (MinistryRecommender::rankByRole)";
    rRole.iterations = iterations;
    rRole.totalTimeMs = roleTimeMs;
    rRole.timePerOpUs = (roleTimeMs / iterations) * 1000.0;
    rRole.throughput = std::to_string(static_cast<int>(roleCandidatesRanked / (roleTimeMs / 1000.0))) + " candidates/sec";
    printResult(rRole);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("openekklesia_benchmarks");

    std::cout << "\n========================================================================================\n"
              << " 🏛️  OpenEkklesia System Performance & Scalability Benchmarks\n"
              << "========================================================================================\n";

    QString dbPath = QDir::tempPath() + QString("/oe_bench_%1.db").arg(QCoreApplication::applicationPid());
    QFile::remove(dbPath);

    if (!DatabaseManager::instance().initialize(dbPath)) {
        std::cerr << "Failed to initialize benchmark database." << std::endl;
        return 1;
    }

    QSqlDatabase db = DatabaseManager::instance().database();

    // Run benchmark suites
    benchmarkGraphPhysics();
    benchmarkDatabaseThroughput(db);
    benchmarkMinistryMatch(db);

    std::cout << "\n========================================================================================\n"
              << " ✅ All benchmark suites completed successfully.\n"
              << "========================================================================================\n\n";

    DatabaseManager::instance().close();
    QFile::remove(dbPath);

    return 0;
}
