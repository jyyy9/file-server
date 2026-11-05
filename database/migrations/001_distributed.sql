-- Additive migration. Run in the selected existing or empty database.
-- MySQL DDL auto-commits; migration runner records version only after every statement succeeds.
CREATE TABLE IF NOT EXISTS ds_schema_version(version INT PRIMARY KEY, applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_allocator(id INT PRIMARY KEY) ENGINE=InnoDB;
INSERT IGNORE INTO ds_allocator VALUES(1);
CREATE TABLE IF NOT EXISTS ds_nodes(
 node_id VARCHAR(64) PRIMARY KEY, address VARCHAR(255) NOT NULL,
 instance CHAR(32) NOT NULL, volume CHAR(32) NOT NULL,
 heartbeat DOUBLE NOT NULL, capacity BIGINT NOT NULL, free_bytes BIGINT NOT NULL,
 reserved BIGINT NOT NULL DEFAULT 0, used_bytes BIGINT NOT NULL DEFAULT 0,
 state VARCHAR(16) NOT NULL, INDEX(state,heartbeat)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_objects(
 object_id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, name VARCHAR(255) NOT NULL,
 size BIGINT NOT NULL, chunk_size INT NOT NULL, replicas INT NOT NULL,
 state VARCHAR(16) NOT NULL, INDEX(owner,state)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_uploads(
 upload_id CHAR(32) PRIMARY KEY, object_id CHAR(32) NOT NULL UNIQUE,
 state VARCHAR(16) NOT NULL, expires DOUBLE NOT NULL, INDEX(state,expires),
 FOREIGN KEY(object_id) REFERENCES ds_objects(object_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_chunks(
 chunk_id CHAR(32) PRIMARY KEY, upload_id CHAR(32) NOT NULL,
 seq INT NOT NULL, length INT NOT NULL, sha256 CHAR(64) NOT NULL DEFAULT '',
 state VARCHAR(16) NOT NULL, UNIQUE KEY(upload_id,seq),
 FOREIGN KEY(upload_id) REFERENCES ds_uploads(upload_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_replicas(
 chunk_id CHAR(32) NOT NULL, node_id VARCHAR(64) NOT NULL,
 volume CHAR(32) NOT NULL, state VARCHAR(16) NOT NULL,
 PRIMARY KEY(chunk_id,node_id), INDEX(node_id,state),
 FOREIGN KEY(chunk_id) REFERENCES ds_chunks(chunk_id),
 FOREIGN KEY(node_id) REFERENCES ds_nodes(node_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_tasks(
 task_id BIGINT PRIMARY KEY AUTO_INCREMENT, kind VARCHAR(16) NOT NULL,
 chunk_id CHAR(32) NOT NULL, node_id VARCHAR(64) NOT NULL DEFAULT '',
 state VARCHAR(16) NOT NULL DEFAULT 'pending', lease DOUBLE NOT NULL DEFAULT 0,
 token CHAR(32) NOT NULL DEFAULT '', attempts INT NOT NULL DEFAULT 0,
 reason VARCHAR(255) NOT NULL DEFAULT '',
 UNIQUE KEY(kind,chunk_id,node_id), INDEX(state,lease)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS ds_instances(
 node_id VARCHAR(64) NOT NULL, instance CHAR(32) NOT NULL,
 PRIMARY KEY(node_id,instance)
) ENGINE=InnoDB;
INSERT IGNORE INTO ds_schema_version(version) VALUES(1);
