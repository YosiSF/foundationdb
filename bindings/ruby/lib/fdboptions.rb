# FoundationDB Ruby API
# Copyright (c) 2013-2017 Apple Inc.

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Documentation for this API can be found at
# https://apple.github.io/foundationdb/api-ruby.html

module FDB
  @@NetworkOption = {
    "LOCAL_ADDRESS" => [10, "Deprecated", '', "IP:PORT"],
    "CLUSTER_FILE" => [20, "Deprecated", '', "path to cluster file"],
    "TRACE_ENABLE" => [30, "Enables trace output to a file in a directory of the clients choosing", '', "path to output directory (or NULL for current working directory)"],
    "TRACE_ROLL_SIZE" => [31, "Sets the maximum size in bytes of a single trace output file. This value should be in the range ``[0, INT64_MAX]``. If the value is set to 0, there is no limit on individual file size. The default is a maximum size of 10,485,760 bytes.", 0, "max size of a single trace output file"],
    "TRACE_MAX_LOGS_SIZE" => [32, "Sets the maximum size of all the trace output files put together. This value should be in the range ``[0, INT64_MAX]``. If the value is set to 0, there is no limit on the total size of the files. The default is a maximum size of 104,857,600 bytes. If the default roll size is used, this means that a maximum of 10 trace files will be written at a time.", 0, "max total size of trace files"],
    "TRACE_LOG_GROUP" => [33, "Sets the 'LogGroup' attribute with the specified value for all events in the trace output files. The default log group is 'default'.", '', "value of the LogGroup attribute"],
    "TRACE_FORMAT" => [34, "Select the format of the log files. xml (the default) and json are supported.", '', "Format of trace files"],
    "TRACE_CLOCK_SOURCE" => [35, "Select clock source for trace files. now (the default) or realtime are supported.", '', "Trace clock source"],
    "TRACE_FILE_IDENTIFIER" => [36, "Once provided, this string will be used to replace the port/PID in the log file names.", '', "The identifier that will be part of all trace file names"],
    "KNOB" => [40, "Set internal tuning or debugging knobs", '', "knob_name=knob_value"],
    "TLS_PLUGIN" => [41, "Deprecated", '', "file path or linker-resolved name"],
    "TLS_CERT_BYTES" => [42, "Set the certificate chain", '', "certificates"],
    "TLS_CERT_PATH" => [43, "Set the file from which to load the certificate chain", '', "file path"],
    "TLS_KEY_BYTES" => [45, "Set the private key corresponding to your own certificate", '', "key"],
    "TLS_KEY_PATH" => [46, "Set the file from which to load the private key corresponding to your own certificate", '', "file path"],
    "TLS_VERIFY_PEERS" => [47, "Set the peer certificate field verification criteria", '', "verification pattern"],
    "BUGGIFY_ENABLE" => [48, "", nil, nil],
    "BUGGIFY_DISABLE" => [49, "", nil, nil],
    "BUGGIFY_SECTION_ACTIVATED_PROBABILITY" => [50, "Set the probability of a BUGGIFY section being active for the current execution.  Only applies to code paths first traversed AFTER this option is changed.", 0, "probability expressed as a percentage between 0 and 100"],
    "BUGGIFY_SECTION_FIRED_PROBABILITY" => [51, "Set the probability of an active BUGGIFY section being fired", 0, "probability expressed as a percentage between 0 and 100"],
    "TLS_CA_BYTES" => [52, "Set the ca bundle", '', "ca bundle"],
    "TLS_CA_PATH" => [53, "Set the file from which to load the certificate authority bundle", '', "file path"],
    "TLS_PASSWORD" => [54, "Set the passphrase for encrypted private key. Password should be set before setting the key for the password to be used.", '', "key passphrase"],
    "DISABLE_MULTI_VERSION_CLIENT_API" => [60, "Disables the multi-version client API and instead uses the local client directly. Must be set before setting up the network.", nil, nil],
    "CALLBACKS_ON_EXTERNAL_THREADS" => [61, "If set, callbacks from external client libraries can be called from threads created by the FoundationDB client library. Otherwise, callbacks will be called from either the thread used to add the callback or the network thread. Setting this option can improve performance when connected using an external client, but may not be safe to use in all environments. Must be set before setting up the network. WARNING: This feature is considered experimental at this time.", nil, nil],
    "EXTERNAL_CLIENT_LIBRARY" => [62, "Adds an external client library for use by the multi-version client API. Must be set before setting up the network.", '', "path to client library"],
    "EXTERNAL_CLIENT_DIRECTORY" => [63, "Searches the specified path for dynamic libraries and adds them to the list of client libraries for use by the multi-version client API. Must be set before setting up the network.", '', "path to directory containing client libraries"],
    "DISABLE_LOCAL_CLIENT" => [64, "Prevents connections through the local client, allowing only connections through externally loaded client libraries.", nil, nil],
    "CLIENT_THREADS_PER_VERSION" => [65, "Spawns multiple worker threads for each version of the client that is loaded.  Setting this to a number greater than one implies disable_local_client.", 0, "Number of client threads to be spawned.  Each cluster will be serviced by a single client thread."],
    "DISABLE_CLIENT_STATISTICS_LOGGING" => [70, "Disables logging of client statistics, such as sampled transaction activity.", nil, nil],
    "ENABLE_SLOW_TASK_PROFILING" => [71, "Deprecated", nil, nil],
    "ENABLE_RUN_LOOP_PROFILING" => [71, "Enables debugging feature to perform run loop profiling. Requires trace logging to be enabled. WARNING: this feature is not recommended for use in production.", nil, nil],
    "CLIENT_BUGGIFY_ENABLE" => [80, "Enable client buggify - will make requests randomly fail (intended for client testing)", nil, nil],
    "CLIENT_BUGGIFY_DISABLE" => [81, "Disable client buggify", nil, nil],
    "CLIENT_BUGGIFY_SECTION_ACTIVATED_PROBABILITY" => [82, "Set the probability of a CLIENT_BUGGIFY section being active for the current execution.", 0, "probability expressed as a percentage between 0 and 100"],
    "CLIENT_BUGGIFY_SECTION_FIRED_PROBABILITY" => [83, "Set the probability of an active CLIENT_BUGGIFY section being fired. A section will only fire if it was activated", 0, "probability expressed as a percentage between 0 and 100"],
  }

  @@DatabaseOption = {
    "LOCATION_CACHE_SIZE" => [10, "Set the size of the client location cache. Raising this value can boost performance in very large databases where clients access data in a near-random pattern. Defaults to 100000.", 0, "Max location cache entries"],
    "MAX_WATCHES" => [20, "Set the maximum number of watches allowed to be outstanding on a database connection. Increasing this number could result in increased resource usage. Reducing this number will not cancel any outstanding watches. Defaults to 10000 and cannot be larger than 1000000.", 0, "Max outstanding watches"],
    "MACHINE_ID" => [21, "Specify the machine ID that was passed to fdbserver processes running on the same machine as this client, for better location-aware load balancing.", '', "Hexadecimal ID"],
    "DATACENTER_ID" => [22, "Specify the datacenter ID that was passed to fdbserver processes running in the same datacenter as this client, for better location-aware load balancing.", '', "Hexadecimal ID"],
    "SNAPSHOT_RYW_ENABLE" => [26, "Snapshot read operations will see the results of writes done in the same transaction. This is the default behavior.", nil, nil],
    "SNAPSHOT_RYW_DISABLE" => [27, "Snapshot read operations will not see the results of writes done in the same transaction. This was the default behavior prior to API version 300.", nil, nil],
    "TRANSACTION_LOGGING_MAX_FIELD_LENGTH" => [405, "Sets the maximum escaped length of key and value fields to be logged to the trace file via the LOG_TRANSACTION option. This sets the ``transaction_logging_max_field_length`` option of each transaction created by this database. See the transaction option description for more information.", 0, "Maximum length of escaped key and value fields."],
    "TRANSACTION_TIMEOUT" => [500, "Set a timeout in milliseconds which, when elapsed, will cause each transaction automatically to be cancelled. This sets the ``timeout`` option of each transaction created by this database. See the transaction option description for more information. Using this option requires that the API version is 610 or higher.", 0, "value in milliseconds of timeout"],
    "TRANSACTION_RETRY_LIMIT" => [501, "Set a maximum number of retries after which additional calls to ``onError`` will throw the most recently seen error code. This sets the ``retry_limit`` option of each transaction created by this database. See the transaction option description for more information.", 0, "number of times to retry"],
    "TRANSACTION_MAX_RETRY_DELAY" => [502, "Set the maximum amount of backoff delay incurred in the call to ``onError`` if the error is retryable. This sets the ``max_retry_delay`` option of each transaction created by this database. See the transaction option description for more information.", 0, "value in milliseconds of maximum delay"],
    "TRANSACTION_SIZE_LIMIT" => [503, "Set the maximum transaction size in bytes. This sets the ``size_limit`` option on each transaction created by this database. See the transaction option description for more information.", 0, "value in bytes"],
    "TRANSACTION_CAUSAL_READ_RISKY" => [504, "The read version will be committed, and usually will be the latest committed, but might not be the latest committed in the event of a simultaneous fault and misbehaving clock.", nil, nil],
    "TRANSACTION_INCLUDE_PORT_IN_ADDRESS" => [505, "Addresses returned by get_addresses_for_key include the port when enabled. As of api version 630, this option is enabled by default and setting this has no effect.", nil, nil],
    "TRANSACTION_BYPASS_UNREADABLE" => [700, "Allows ``get`` operations to read from sections of keyspace that have become unreadable because of versionstamp operations. This sets the ``bypass_unreadable`` option of each transaction created by this database. See the transaction option description for more information.", nil, nil],
  }

  @@TransactionOption = {
    "CAUSAL_WRITE_RISKY" => [10, "The transaction, if not self-conflicting, may be committed a second time after commit succeeds, in the event of a fault", nil, nil],
    "CAUSAL_READ_RISKY" => [20, "The read version will be committed, and usually will be the latest committed, but might not be the latest committed in the event of a simultaneous fault and misbehaving clock.", nil, nil],
    "CAUSAL_READ_DISABLE" => [21, "", nil, nil],
    "INCLUDE_PORT_IN_ADDRESS" => [23, "Addresses returned by get_addresses_for_key include the port when enabled. As of api version 630, this option is enabled by default and setting this has no effect.", nil, nil],
    "NEXT_WRITE_NO_WRITE_CONFLICT_RANGE" => [30, "The next write performed on this transaction will not generate a write conflict range. As a result, other transactions which read the key(s) being modified by the next write will not conflict with this transaction. Care needs to be taken when using this option on a transaction that is shared between multiple threads. When setting this option, write conflict ranges will be disabled on the next write operation, regardless of what thread it is on.", nil, nil],
    "READ_YOUR_WRITES_DISABLE" => [51, "Reads performed by a transaction will not see any prior mutations that occured in that transaction, instead seeing the value which was in the database at the transaction's read version. This option may provide a small performance benefit for the client, but also disables a number of client-side optimizations which are beneficial for transactions which tend to read and write the same keys within a single transaction.", nil, nil],
    "READ_AHEAD_DISABLE" => [52, "Deprecated", nil, nil],
    "DURABILITY_DATACENTER" => [110, "", nil, nil],
    "DURABILITY_RISKY" => [120, "", nil, nil],
    "DURABILITY_DEV_NULL_IS_WEB_SCALE" => [130, "Deprecated", nil, nil],
    "PRIORITY_SYSTEM_IMMEDIATE" => [200, "Specifies that this transaction should be treated as highest priority and that lower priority transactions should block behind this one. Use is discouraged outside of low-level tools", nil, nil],
    "PRIORITY_BATCH" => [201, "Specifies that this transaction should be treated as low priority and that default priority transactions will be processed first. Batch priority transactions will also be throttled at load levels smaller than for other types of transactions and may be fully cut off in the event of machine failures. Useful for doing batch work simultaneously with latency-sensitive work", nil, nil],
    "INITIALIZE_NEW_DATABASE" => [300, "This is a write-only transaction which sets the initial configuration. This option is designed for use by database system tools only.", nil, nil],
    "ACCESS_SYSTEM_KEYS" => [301, "Allows this transaction to read and modify system keys (those that start with the byte 0xFF)", nil, nil],
    "READ_SYSTEM_KEYS" => [302, "Allows this transaction to read system keys (those that start with the byte 0xFF)", nil, nil],
    "DEBUG_RETRY_LOGGING" => [401, "", '', "Optional transaction name"],
    "TRANSACTION_LOGGING_ENABLE" => [402, "Deprecated", '', "String identifier to be used in the logs when tracing this transaction. The identifier must not exceed 100 characters."],
    "DEBUG_TRANSACTION_IDENTIFIER" => [403, "Sets a client provided identifier for the transaction that will be used in scenarios like tracing or profiling. Client trace logging or transaction profiling must be separately enabled.", '', "String identifier to be used when tracing or profiling this transaction. The identifier must not exceed 100 characters."],
    "LOG_TRANSACTION" => [404, "Enables tracing for this transaction and logs results to the client trace logs. The DEBUG_TRANSACTION_IDENTIFIER option must be set before using this option, and client trace logging must be enabled to get log output.", nil, nil],
    "TRANSACTION_LOGGING_MAX_FIELD_LENGTH" => [405, "Sets the maximum escaped length of key and value fields to be logged to the trace file via the LOG_TRANSACTION option, after which the field will be truncated. A negative value disables truncation.", 0, "Maximum length of escaped key and value fields."],
    "SERVER_REQUEST_TRACING" => [406, "Sets an identifier for server tracing of this transaction. When committed, this identifier triggers logging when each part of the transaction authority encounters it, which is helpful in diagnosing slowness in misbehaving clusters. The identifier is randomly generated. When there is also a debug_transaction_identifier, both IDs are logged together.", nil, nil],
    "TIMEOUT" => [500, "Set a timeout in milliseconds which, when elapsed, will cause the transaction automatically to be cancelled. Valid parameter values are ``[0, INT_MAX]``. If set to 0, will disable all timeouts. All pending and any future uses of the transaction will throw an exception. The transaction can be used again after it is reset. Prior to API version 610, like all other transaction options, the timeout must be reset after a call to ``onError``. If the API version is 610 or greater, the timeout is not reset after an ``onError`` call. This allows the user to specify a longer timeout on specific transactions than the default timeout specified through the ``transaction_timeout`` database option without the shorter database timeout cancelling transactions that encounter a retryable error. Note that at all API versions, it is safe and legal to set the timeout each time the transaction begins, so most code written assuming the older behavior can be upgraded to the newer behavior without requiring any modification, and the caller is not required to implement special logic in retry loops to only conditionally set this option.", 0, "value in milliseconds of timeout"],
    "RETRY_LIMIT" => [501, "Set a maximum number of retries after which additional calls to ``onError`` will throw the most recently seen error code. Valid parameter values are ``[-1, INT_MAX]``. If set to -1, will disable the retry limit. Prior to API version 610, like all other transaction options, the retry limit must be reset after a call to ``onError``. If the API version is 610 or greater, the retry limit is not reset after an ``onError`` call. Note that at all API versions, it is safe and legal to set the retry limit each time the transaction begins, so most code written assuming the older behavior can be upgraded to the newer behavior without requiring any modification, and the caller is not required to implement special logic in retry loops to only conditionally set this option.", 0, "number of times to retry"],
    "MAX_RETRY_DELAY" => [502, "Set the maximum amount of backoff delay incurred in the call to ``onError`` if the error is retryable. Defaults to 1000 ms. Valid parameter values are ``[0, INT_MAX]``. If the maximum retry delay is less than the current retry delay of the transaction, then the current retry delay will be clamped to the maximum retry delay. Prior to API version 610, like all other transaction options, the maximum retry delay must be reset after a call to ``onError``. If the API version is 610 or greater, the retry limit is not reset after an ``onError`` call. Note that at all API versions, it is safe and legal to set the maximum retry delay each time the transaction begins, so most code written assuming the older behavior can be upgraded to the newer behavior without requiring any modification, and the caller is not required to implement special logic in retry loops to only conditionally set this option.", 0, "value in milliseconds of maximum delay"],
    "SIZE_LIMIT" => [503, "Set the transaction size limit in bytes. The size is calculated by combining the sizes of all keys and values written or mutated, all key ranges cleared, and all read and write conflict ranges. (In other words, it includes the total size of all data included in the request to the cluster to commit the transaction.) Large transactions can cause performance problems on FoundationDB clusters, so setting this limit to a smaller value than the default can help prevent the client from accidentally degrading the cluster's performance. This value must be at least 32 and cannot be set to higher than 10,000,000, the default transaction size limit.", 0, "value in bytes"],
    "SNAPSHOT_RYW_ENABLE" => [600, "Snapshot read operations will see the results of writes done in the same transaction. This is the default behavior.", nil, nil],
    "SNAPSHOT_RYW_DISABLE" => [601, "Snapshot read operations will not see the results of writes done in the same transaction. This was the default behavior prior to API version 300.", nil, nil],
    "LOCK_AWARE" => [700, "The transaction can read and write to locked databases, and is responsible for checking that it took the lock.", nil, nil],
    "USED_DURING_COMMIT_PROTECTION_DISABLE" => [701, "By default, operations that are performed on a transaction while it is being committed will not only fail themselves, but they will attempt to fail other in-flight operations (such as the commit) as well. This behavior is intended to help developers discover situations where operations could be unintentionally executed after the transaction has been reset. Setting this option removes that protection, causing only the offending operation to fail.", nil, nil],
    "READ_LOCK_AWARE" => [702, "The transaction can read from locked databases.", nil, nil],
    "USE_PROVISIONAL_PROXIES" => [711, "This option should only be used by tools which change the database configuration.", nil, nil],
    "REPORT_CONFLICTING_KEYS" => [712, "The transaction can retrieve keys that are conflicting with other transactions.", nil, nil],
    "SPECIAL_KEY_SPACE_RELAXED" => [713, "By default, the special key space will only allow users to read from exactly one module (a subspace in the special key space). Use this option to allow reading from zero or more modules. Users who set this option should be prepared for new modules, which may have different behaviors than the modules they're currently reading. For example, a new module might block or return an error.", nil, nil],
    "TAG" => [800, "Adds a tag to the transaction that can be used to apply manual targeted throttling. At most 5 tags can be set on a transaction.", '', "String identifier used to associated this transaction with a throttling group. Must not exceed 16 characters."],
    "AUTO_THROTTLE_TAG" => [801, "Adds a tag to the transaction that can be used to apply manual or automatic targeted throttling. At most 5 tags can be set on a transaction.", '', "String identifier used to associated this transaction with a throttling group. Must not exceed 16 characters."],
    "BYPASS_UNREADABLE" => [1100, "Allows ``get`` operations to read from sections of keyspace that have become unreadable because of versionstamp operations. These reads will view versionstamp operations as if they were set operations that did not fill in the versionstamp.", nil, nil],
  }

  @@StreamingMode = {
    "WANT_ALL" => [-2, "Client intends to consume the entire range and would like it all transferred as early as possible.", nil, nil],
    "ITERATOR" => [-1, "The default. The client doesn't know how much of the range it is likely to used and wants different performance concerns to be balanced. Only a small portion of data is transferred to the client initially (in order to minimize costs if the client doesn't read the entire range), and as the caller iterates over more items in the range larger batches will be transferred in order to minimize latency. After enough iterations, the iterator mode will eventually reach the same byte limit as ``WANT_ALL``", nil, nil],
    "EXACT" => [0, "Infrequently used. The client has passed a specific row limit and wants that many rows delivered in a single batch. Because of iterator operation in client drivers make request batches transparent to the user, consider ``WANT_ALL`` StreamingMode instead. A row limit must be specified if this mode is used.", nil, nil],
    "SMALL" => [1, "Infrequently used. Transfer data in batches small enough to not be much more expensive than reading individual rows, to minimize cost if iteration stops early.", nil, nil],
    "MEDIUM" => [2, "Infrequently used. Transfer data in batches sized in between small and large.", nil, nil],
    "LARGE" => [3, "Infrequently used. Transfer data in batches large enough to be, in a high-concurrency environment, nearly as efficient as possible. If the client stops iteration early, some disk and network bandwidth may be wasted. The batch size may still be too small to allow a single client to get high throughput from the database, so if that is what you need consider the SERIAL StreamingMode.", nil, nil],
    "SERIAL" => [4, "Transfer data in batches large enough that an individual client can get reasonable read bandwidth from the database. If the client stops iteration early, considerable disk and network bandwidth may be wasted.", nil, nil],
  }

  @@MutationType = {
    "ADD" => [2, "Performs an addition of little-endian integers. If the existing value in the database is not present or shorter than ``param``, it is first extended to the length of ``param`` with zero bytes.  If ``param`` is shorter than the existing value in the database, the existing value is truncated to match the length of ``param``. The integers to be added must be stored in a little-endian representation.  They can be signed in two's complement representation or unsigned. You can add to an integer at a known offset in the value by prepending the appropriate number of zero bytes to ``param`` and padding with zero bytes to match the length of the value. However, this offset technique requires that you know the addition will not cause the integer field within the value to overflow.", '', "addend"],
    "AND" => [6, "Deprecated", '', "value with which to perform bitwise and"],
    "BIT_AND" => [6, "Performs a bitwise ``and`` operation.  If the existing value in the database is not present, then ``param`` is stored in the database. If the existing value in the database is shorter than ``param``, it is first extended to the length of ``param`` with zero bytes.  If ``param`` is shorter than the existing value in the database, the existing value is truncated to match the length of ``param``.", '', "value with which to perform bitwise and"],
    "OR" => [7, "Deprecated", '', "value with which to perform bitwise or"],
    "BIT_OR" => [7, "Performs a bitwise ``or`` operation.  If the existing value in the database is not present or shorter than ``param``, it is first extended to the length of ``param`` with zero bytes.  If ``param`` is shorter than the existing value in the database, the existing value is truncated to match the length of ``param``.", '', "value with which to perform bitwise or"],
    "XOR" => [8, "Deprecated", '', "value with which to perform bitwise xor"],
    "BIT_XOR" => [8, "Performs a bitwise ``xor`` operation.  If the existing value in the database is not present or shorter than ``param``, it is first extended to the length of ``param`` with zero bytes.  If ``param`` is shorter than the existing value in the database, the existing value is truncated to match the length of ``param``.", '', "value with which to perform bitwise xor"],
    "APPEND_IF_FITS" => [9, "Appends ``param`` to the end of the existing value already in the database at the given key (or creates the key and sets the value to ``param`` if the key is empty). This will only append the value if the final concatenated value size is less than or equal to the maximum value size (i.e., if it fits). WARNING: No error is surfaced back to the user if the final value is too large because the mutation will not be applied until after the transaction has been committed. Therefore, it is only safe to use this mutation type if one can guarantee that one will keep the total value size under the maximum size.", '', "value to append to the database value"],
    "MAX" => [12, "Performs a little-endian comparison of byte strings. If the existing value in the database is not present or shorter than ``param``, it is first extended to the length of ``param`` with zero bytes.  If ``param`` is shorter than the existing value in the database, the existing value is truncated to match the length of ``param``. The larger of the two values is then stored in the database.", '', "value to check against database value"],
    "MIN" => [13, "Performs a little-endian comparison of byte strings. If the existing value in the database is not present, then ``param`` is stored in the database. If the existing value in the database is shorter than ``param``, it is first extended to the length of ``param`` with zero bytes.  If ``param`` is shorter than the existing value in the database, the existing value is truncated to match the length of ``param``. The smaller of the two values is then stored in the database.", '', "value to check against database value"],
    "SET_VERSIONSTAMPED_KEY" => [14, "Transforms ``key`` using a versionstamp for the transaction. Sets the transformed key in the database to ``param``. The key is transformed by removing the final four bytes from the key and reading those as a little-Endian 32-bit integer to get a position ``pos``. The 10 bytes of the key from ``pos`` to ``pos + 10`` are replaced with the versionstamp of the transaction used. The first byte of the key is position 0. A versionstamp is a 10 byte, unique, monotonically (but not sequentially) increasing value for each committed transaction. The first 8 bytes are the committed version of the database (serialized in big-Endian order). The last 2 bytes are monotonic in the serialization order for transactions. WARNING: At this time, versionstamps are compatible with the Tuple layer only in the Java, Python, and Go bindings. Also, note that prior to API version 520, the offset was computed from only the final two bytes rather than the final four bytes.", '', "value to which to set the transformed key"],
    "SET_VERSIONSTAMPED_VALUE" => [15, "Transforms ``param`` using a versionstamp for the transaction. Sets the ``key`` given to the transformed ``param``. The parameter is transformed by removing the final four bytes from ``param`` and reading those as a little-Endian 32-bit integer to get a position ``pos``. The 10 bytes of the parameter from ``pos`` to ``pos + 10`` are replaced with the versionstamp of the transaction used. The first byte of the parameter is position 0. A versionstamp is a 10 byte, unique, monotonically (but not sequentially) increasing value for each committed transaction. The first 8 bytes are the committed version of the database (serialized in big-Endian order). The last 2 bytes are monotonic in the serialization order for transactions. WARNING: At this time, versionstamps are compatible with the Tuple layer only in the Java, Python, and Go bindings. Also, note that prior to API version 520, the versionstamp was always placed at the beginning of the parameter rather than computing an offset.", '', "value to versionstamp and set"],
    "BYTE_MIN" => [16, "Performs lexicographic comparison of byte strings. If the existing value in the database is not present, then ``param`` is stored. Otherwise the smaller of the two values is then stored in the database.", '', "value to check against database value"],
    "BYTE_MAX" => [17, "Performs lexicographic comparison of byte strings. If the existing value in the database is not present, then ``param`` is stored. Otherwise the larger of the two values is then stored in the database.", '', "value to check against database value"],
    "COMPARE_AND_CLEAR" => [20, "Performs an atomic ``compare and clear`` operation. If the existing value in the database is equal to the given value, then given key is cleared.", '', "Value to compare with"],
  }

  @@ConflictRangeType = {
    "READ" => [0, "Used to add a read conflict range", nil, nil],
    "WRITE" => [1, "Used to add a write conflict range", nil, nil],
  }

  @@ErrorPredicate = {

  }

end
