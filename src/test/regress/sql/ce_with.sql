\! gs_ktool -d all
\! gs_ktool -g

DROP CLIENT MASTER KEY IF EXISTS MyCMK CASCADE;
CREATE CLIENT MASTER KEY MyCMK WITH ( KEY_STORE = gs_ktool , KEY_PATH = "gs_ktool/1" , ALGORITHM = AES_256_CBC);
CREATE COLUMN ENCRYPTION KEY MyCEK WITH VALUES (CLIENT_MASTER_KEY = MyCMK, ALGORITHM = AEAD_AES_256_CBC_HMAC_SHA256);
CREATE COLUMN ENCRYPTION KEY MyCEK1 WITH VALUES (CLIENT_MASTER_KEY = MyCMK, ALGORITHM = AEAD_AES_256_CBC_HMAC_SHA256);
CREATE TABLE IF NOT EXISTS t_num(id INT, num int ENCRYPTED WITH (COLUMN_ENCRYPTION_KEY = MyCEK, ENCRYPTION_TYPE = DETERMINISTIC));
INSERT INTO t_num (id, num) VALUES (1, 555);
INSERT INTO t_num (id, num) VALUES (2, 666666);
INSERT INTO t_num (id, num) VALUES (3, 777777);
SELECT * from t_num order by id limit 1;
WITH temp AS (select * from t_num order by id limit 1) select * from temp;
WITH temp AS (select * from t_num order by id limit 2) select * from temp;


CREATE TABLE COMPANY1(
   ID INT PRIMARY KEY NOT NULL, SALARY INT NOT NULL ENCRYPTED WITH (COLUMN_ENCRYPTION_KEY = MyCEK, ENCRYPTION_TYPE = DETERMINISTIC)
);

CREATE TABLE COMPANY2(
   ID INT PRIMARY KEY NOT NULL, SALARY INT NOT NULL ENCRYPTED WITH (COLUMN_ENCRYPTION_KEY = MyCEK1, ENCRYPTION_TYPE = DETERMINISTIC)
);

CREATE TABLE COMPANY3(
   ID INT PRIMARY KEY NOT NULL, SALARY INT NOT NULL ENCRYPTED WITH (COLUMN_ENCRYPTION_KEY = MyCEK, ENCRYPTION_TYPE = DETERMINISTIC)
);
INSERT INTO COMPANY1 (ID, SALARY) VALUES (1, 1000), (2, 2000), (3, 3000), (4, 4000);
INSERT INTO COMPANY2 (ID, SALARY) VALUES (5, 1000), (6, 2000), (7, 3000), (8, 4000);
INSERT INTO COMPANY3 (ID, SALARY) VALUES (5, 1000), (6, 2000), (7, 3000), (8, 4000);

WITH moved_rows AS (
   DELETE FROM COMPANY3
   WHERE
      ID >= 3
   RETURNING *
)
INSERT INTO COMPANY1 (SELECT * FROM moved_rows);

SELECT * FROM COMPANY1 order by ID;

WITH moved_rows AS (
   DELETE FROM COMPANY2
   WHERE
      ID >= 3
   RETURNING *
)
INSERT INTO COMPANY1 (SELECT * FROM moved_rows);

select * from COMPANY1 order by ID;

--support
WITH temp AS (select * from t_num where id = 1) select * from temp;
--fail
WITH temp AS (select * from t_num where num = 555) select * from temp;

DROP TABLE t_num;
DROP TABLE COMPANY1;
DROP TABLE COMPANY2;
DROP TABLE COMPANY3;
DROP COLUMN ENCRYPTION KEY MyCEK;
DROP COLUMN ENCRYPTION KEY MyCEK1;
DROP CLIENT MASTER KEY MyCMK;

\! gs_ktool -d all