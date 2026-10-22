CREATE TABLE students (
    id INT,
    name STRING,
    cgpa FLOAT,
    active BOOL
);
INSERT INTO students VALUES (1, 'Aman', 8.5, true);
INSERT INTO students VALUES (2, 'O''Brien, "Alex"', 7.2, false);
INSERT INTO students VALUES (3, 'First line
Second line', 9.1, true);
SELECT * FROM students;
SELECT name, cgpa FROM students WHERE cgpa > 8.0;
SELECT name FROM students WHERE active = true;
UPDATE students SET cgpa = 9.2 WHERE id = 1;
SELECT * FROM students WHERE id = 1;
DELETE FROM students WHERE id = 2;
SELECT * FROM students;
DROP TABLE students;
