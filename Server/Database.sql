DROP DATABASE IF EXISTS ArchivioVideo;
CREATE DATABASE ArchivioVideo;
USE ArchivioVideo;

CREATE TABLE telegram_video(id int PRIMARY KEY AUTO_INCREMENT,
                            message_id bigint,
                            chat_id bigint);

CREATE TABLE image(id int PRIMARY KEY AUTO_INCREMENT,
                   original_filename varchar(255),
                   saved_filename varchar(255));

CREATE TABLE video(id INT PRIMARY KEY AUTO_INCREMENT,
                   title varchar(255),
                   description text,
                   data_caricamento date,
                   telegram_video_id int,
                   thumbnail_id int,
                   FOREIGN KEY(telegram_video_id) REFERENCES telegram_video(id),
                   FOREIGN KEY(thumbnail_id) REFERENCES image(id));