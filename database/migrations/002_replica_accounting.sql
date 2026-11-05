-- Accounting follows replica rows, including deleted data awaiting physical cleanup.
ALTER TABLE ds_replicas ADD COLUMN charged VARCHAR(16) NOT NULL DEFAULT 'reserved';
UPDATE ds_replicas SET charged=CASE WHEN state IN ('valid','bad','unverified','deleting') THEN 'used' WHEN state='gone' THEN 'none' ELSE 'reserved' END;
INSERT INTO ds_schema_version(version) VALUES(2);
