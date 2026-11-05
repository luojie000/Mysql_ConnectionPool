# MySQL C++ 客户端连接 Demo

用 C/C++ 官方 API（`libmysqlclient`）连接 MySQL 8 的最小可运行示例。

**目的**：把「C++ 程序怎么连上数据库」这条链路验证一遍——建连、设字符集、执行查询、遍历结果集、释放资源。所有错误分支都有明确处理，可以作为后续封装**连接池**的起点。

---

## 目录

- [能学到什么](#能学到什么)
- [完整流程](#完整流程)
- [环境准备](#环境准备)
- [编译运行](#编译运行)
- [逐段代码解析](#逐段代码解析)
- [常见错误排查](#常见错误排查)
- [下一步：连接池](#下一步连接池)

---

## 能学到什么

| 知识点 | 对应代码 |
|---|---|
| `mysql_init` / `mysql_real_connect` 的调用顺序 | 初始化 + 建连 |
| **密码不写进代码**，从环境变量读取 | `std::getenv("MYSQL_PASSWORD")` |
| 字符集必须在查询之前设置 | `mysql_set_character_set(conn, "utf8mb4")` |
| `mysql_store_result` vs `mysql_use_result` 的取舍 | 结果集获取 |
| `MYSQL_ROW` 里 NULL 的处理 | `row[i] != nullptr` 判断 |
| 四条资源释放路径不能漏 | `mysql_free_result` / `mysql_close` |

---

## 完整流程

```
mysql_init(nullptr)
      │  失败 → nullptr
      ▼
读取 MYSQL_PASSWORD 环境变量
      │  未设置 → 立即退出（不要在代码里硬编码密码）
      ▼
mysql_real_connect(host, user, passwd, db, port, ...)
      │  失败 → mysql_error(conn) 打印原因
      ▼
mysql_set_character_set(conn, "utf8mb4")
      │  必须在所有查询之前
      ▼
mysql_query(conn, "SELECT id,username,email,age FROM users")
      │  失败 → 打印 mysql_error
      ▼
mysql_store_result(conn)  ← 一次性把整个结果集拉到客户端
      │
      ▼
while (mysql_fetch_row(result)) { 遍历每一行 }
      │
      ▼
mysql_free_result(result) → mysql_close(conn)
```

---

## 环境准备

### 1. 安装 MySQL 客户端开发库

| 系统 | 命令 |
|---|---|
| **Ubuntu / Debian** | `sudo apt install libmysqlclient-dev` |
| **CentOS / RHEL** | `sudo yum install mysql-devel` |
| **Arch** | `sudo pacman -S mariadb-libs` |
| **macOS** | `brew install mysql-client` |
| **Windows** | 安装 MySQL Connector/C 或用 vcpkg：`vcpkg install libmysql` |

### 2. 创建数据库与用户

Demo 期望的表结构（字段与 SQL 语句对应）：

```sql
CREATE DATABASE IF NOT EXISTS connection_pool_test
  DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE connection_pool_test;

CREATE TABLE users (
    id       INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64)  NOT NULL,
    email    VARCHAR(128),
    age      INT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO users (username, email, age) VALUES
    ('alice', 'alice@example.com', 22),
    ('bob',   'bob@example.com',   25),
    ('carol', NULL,                19);
```

创建专用账号（**不要直接用 root 跑应用**）：

```sql
CREATE USER 'pool_user'@'localhost' IDENTIFIED BY '你的强密码';
GRANT SELECT, INSERT, UPDATE, DELETE ON connection_pool_test.* TO 'pool_user'@'localhost';
FLUSH PRIVILEGES;
```

> ⚠️ 上面这条 `CREATE USER` 中的密码就是后面要传给 `MYSQL_PASSWORD` 环境变量的值。

### 3. 设置密码环境变量

代码通过 `std::getenv("MYSQL_PASSWORD")` 读取密码，**任何情况下都不要把密码写进源码**。

**Linux / macOS**

```bash
export MYSQL_PASSWORD='你的强密码'
```

**Windows（PowerShell）**

```powershell
$env:MYSQL_PASSWORD = '你的强密码'
```

**Windows（CMD）**

```cmd
set MYSQL_PASSWORD=你的强密码
```

> 想永久生效请写入 `~/.bashrc` / `~/.zshrc`（Linux/macOS）或系统环境变量（Windows）。**不要**写入会被提交到 Git 的文件。

### 4. 配置连接参数

`demo/main.cpp` 中这几处与实际环境相关，按需修改：

```cpp
MYSQL* connected = mysql_real_connect(
    conn,
    "127.0.0.1",              // 数据库地址
    "pool_user",               // 用户名
    password,                  // 密码（来自环境变量）
    "connection_pool_test",    // 数据库名
    3306,                      // 端口
    nullptr,                   // unix socket（nullptr 表示用 TCP）
    0                          // 客户端标志位
);
```

---

## 编译运行

### 方式一：CMake（推荐）

```bash
cd demo
cmake -B build -S .
cmake --build build
```

`CMakeLists.txt` 通过 `pkg-config` 查找 `mysqlclient`：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED IMPORTED_TARGET mysqlclient)
target_link_libraries(mysql_demo PRIVATE PkgConfig::MYSQL)
```

### 方式二：直接 g++

```bash
g++ -std=c++17 main.cpp -o mysql_demo $(pkg-config --cflags --libs mysqlclient)
```

没有 `pkg-config` 时手工指定：

```bash
g++ -std=c++17 main.cpp -o mysql_demo -I/usr/include/mysql -lmysqlclient
```

### 运行

```bash
./build/mysql_demo
```

### 预期输出

```
mysql connected successfully!
query executed successfully
1|alice|alice@example.com|22
2|bob|bob@example.com|25
3|carol|null|19
mysql connection closed
```

注意第三行的 `null` —— 这是代码显式处理的：`row[i]` 对 SQL NULL 返回空指针，直接用 `std::cout << row[i]` 会**输出未定义内容甚至崩溃**。

---

## 逐段代码解析

### ① 初始化与建连

```cpp
MYSQL* conn = mysql_init(nullptr);
if (conn == nullptr) { /* 内存不足，极少发生 */ }

MYSQL* connected = mysql_real_connect(
    conn, "127.0.0.1", "pool_user", password,
    "connection_pool_test", 3306, nullptr, 0
);
if (connected == nullptr) { /* 建连失败 */ }
```

**易错点**：`mysql_real_connect` 成功时返回的指针与传入的 `conn` **是同一个**，但失败时返回 `nullptr`，而 `conn` 依然有效（可以用于 `mysql_error(conn)` 取错误信息）。**不要在失败分支里丢失 `conn` 引用**，否则无法打印错误原因——这正是代码里先保存 `connected` 再判断的原因。

### ② 字符集必须显式设置

```cpp
if (mysql_set_character_set(conn, "utf8mb4") != 0) { /* ... */ }
```

MySQL 默认连接字符集取决于服务端配置，可能是 `latin1`。**不设字符集时中文会变成 `????`** ——而且这个错误不报异常，只在数据里静静腐烂。所以必须在 `mysql_real_connect` 之后、任何查询之前调用。

`utf8mb4` 而不是 `utf8`：MySQL 的 `utf8` 实际是 `utf8mb3`，最多 3 字节，**存不了 emoji 和部分生僻字**。`utf8mb4` 才是真正的 UTF-8。

### ③ 执行查询

```cpp
const char* sql = "SELECT id,username,email,age FROM users";
if (mysql_query(conn, sql) != 0) { /* 打印 mysql_error(conn) */ }
```

**所有返回值都要检查**。`mysql_query` 返回非 0 意味着 SQL 语法错误、表不存在、权限不足、连接断开……具体原因只能通过 `mysql_error(conn)` 得知。

### ④ 获取结果集：store vs use

```cpp
MYSQL_RES* result = mysql_store_result(conn);
```

| | `mysql_store_result` | `mysql_use_result` |
|---|---|---|
| 行为 | **一次性**把全部结果拉到客户端内存 | 逐行从服务器流式读取 |
| 内存 | 占用 = 结果集大小，大表可能 OOM | 只占一行缓冲 |
| 灵活性 | 可 `mysql_num_rows`、可随意移动 | **必须**按顺序读完，不能打断 |
| 适用 | 结果集小、需要随机访问（本 Demo） | 结果集巨大、边读边处理 |

本 Demo 用 `store`，因为要直接遍历且数据量小。**处理百万行报表时应换 `use_result`**。

> 注意：如果执行的 SQL **不返回结果集**（如 `INSERT`、`UPDATE`），`mysql_store_result` 返回 `nullptr` 是**正常**的，不是错误。代码里把 `nullptr` 一律当错误处理，在纯 `SELECT` 场景下没问题，但通用封装时需要区分 `mysql_field_count(conn) == 0` 的情况。

### ⑤ 遍历与 NULL 处理

```cpp
unsigned int fields = mysql_num_fields(result);
MYSQL_ROW row;
while ((row = mysql_fetch_row(result)) != nullptr) {
    for (unsigned int i = 0; i < fields; i++) {
        if (row[i] != nullptr) std::cout << row[i];
        else                   std::cout << "null";   // ← 关键
    }
}
```

`MYSQL_ROW` 本质是 `char**`，每个字段都是**字符串**（哪怕数据库里是 `INT`，拿回来的也是 `"22"`）。要做数值运算需要自己 `std::stoi` / `std::stod`，并且注意异常处理。

`mysql_fetch_row` 返回 `nullptr` 有两种含义：**遍历结束**，或**出错**。严格实现应额外检查 `mysql_errno(conn)`：

```cpp
if (row == nullptr && mysql_errno(conn) != 0) {
    // 真正的错误
}
```

### ⑥ 资源释放

```cpp
mysql_free_result(result);
mysql_close(conn);
```

四个必须配对的调用：

| 分配 | 释放 |
|---|---|
| `mysql_init()` | `mysql_close()` |
| `mysql_store_result()` | `mysql_free_result()` |
| `mysql_fetch_lengths()` | 无需释放 |
| `mysql_real_escape_string` 的返回值 | 无需释放（写入调用者缓冲） |

**漏掉 `mysql_free_result` 会泄漏整个结果集的内存**——不是几字节，是 `行数 × 行宽`。这是连接池实现中最常见的泄漏点。

---

## 常见错误排查

| 报错信息 | 原因 | 解决 |
|---|---|---|
| `MYSQL PASSWORD IS NOT SET` | 环境变量没设 | 见 [设置密码环境变量](#3-设置密码环境变量)；注意 `export` 只对当前终端生效 |
| `Can't connect to MySQL server on '127.0.0.1' (111)` | MySQL 没启动，或只监听了 socket | `systemctl status mysql`；检查 `bind-address` |
| `Access denied for user 'pool_user'@'localhost'` | 密码错 / 用户不存在 / 权限不足 | `mysql -u pool_user -p` 手工验证；注意 `localhost` 与 `127.0.0.1` 在 MySQL 里是**两个不同的 host** |
| `Unknown database 'connection_pool_test'` | 数据库没建 | 执行上面的 `CREATE DATABASE` |
| `Table 'connection_pool_test.users' doesn't exist` | 表没建 | 执行上面的 `CREATE TABLE` |
| `Plugin caching_sha2_password could not be loaded` | MySQL 8 默认认证插件与旧版客户端不兼容 | 升级 `libmysqlclient`，或 `ALTER USER ... IDENTIFIED WITH mysql_native_password BY '...'` |
| `Package mysqlclient was not found`（CMake） | 缺开发库 | `apt install libmysqlclient-dev`；确认 `pkg-config --exists mysqlclient` 返回 0 |
| 中文显示为 `????` | 字符集未设或设错 | 确认 `mysql_set_character_set(conn,"utf8mb4")` 在查询之前；库和表也用 utf8mb4 |

### 手工验证连接（排查第一步）

在怀疑代码之前，先用命令行验证账号可用：

```bash
mysql -h 127.0.0.1 -P 3306 -u pool_user -p connection_pool_test
mysql> SELECT id, username FROM users;
```

命令能通而程序不通，问题就在代码或编译链接；命令也不通，问题在 MySQL 配置。

---

## 下一步：连接池

这个 Demo 是**单连接顺序使用**，无法支撑并发。真实服务需要连接池，要解决四个问题：

| 问题 | 方向 |
|---|---|
| 连接复用 | 预建 N 个连接放进队列，用完归还而非关闭 |
| 线程安全 | `std::mutex` + `std::condition_variable` 保护队列；或引入无锁队列 |
| 断线重连 | 借出前用 `mysql_ping(conn)` 检测，失败则 `mysql_close` 后重建 |
| 优雅关闭 | 关闭时唤醒所有等待者并返回失败，而非让它们永久阻塞 |

`mysql_ping` 是关键——MySQL 有 `wait_timeout`（默认 8 小时），空闲连接会被服务端单方面断开，客户端**不会收到通知**。所以从池里取出的连接必须先 ping。

---

## 项目结构

```
.
├── .gitignore              # 忽略 build/、CMakeCache.txt、*.o 等构建产物
└── demo/
    ├── CMakeLists.txt      # pkg-config 查找 mysqlclient
    └── main.cpp            # 单连接 Demo
```

---

## License

未指定。
