-- 创建数据库
CREATE DATABASE IF NOT EXISTS chat_app;
USE chat_app;

-- 创建 users 表
CREATE TABLE users (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '用户 ID',
    username VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名',
    password VARCHAR(255) NOT NULL COMMENT '加密密码',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '用户创建时间'
) AUTO_INCREMENT = 113280 COMMENT='用户表';

-- 创建 friends 表
CREATE TABLE friends (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '关系 ID',
    user_id INT UNSIGNED NOT NULL COMMENT '用户 ID',
    friend_id INT UNSIGNED NOT NULL COMMENT '好友 ID',
    status ENUM('pending', 'accepted', 'blocked') DEFAULT 'pending' COMMENT '好友关系状态',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (friend_id) REFERENCES users(id) ON DELETE CASCADE
) COMMENT='好友关系表';

-- 创建 groups 表
CREATE TABLE groups (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '群组 ID',
    group_name VARCHAR(100) NOT NULL COMMENT '群组名称',
    creator_id INT UNSIGNED NOT NULL COMMENT '创建者 ID',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    FOREIGN KEY (creator_id) REFERENCES users(id) ON DELETE CASCADE
) COMMENT='群组表';

-- 创建 group_members 表
CREATE TABLE group_members (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '成员记录 ID',
    group_id INT UNSIGNED NOT NULL COMMENT '群组 ID',
    user_id INT UNSIGNED NOT NULL COMMENT '用户 ID',
    joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '加入时间',
    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) COMMENT='群组成员表';

 -- 群邀请消息表
CREATE TABLE group_invites (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    group_id INT UNSIGNED NOT NULL,
    sender_id INT UNSIGNED NOT NULL,
    invitee_id INT UNSIGNED NOT NULL,
    status ENUM('pending', 'accepted', 'rejected') NOT NULL DEFAULT 'pending',
    sent_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE,
    FOREIGN KEY (sender_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (invitee_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 创建 offline_messages 表
CREATE TABLE offline_messages (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '消息记录 ID',
    sender_id INT UNSIGNED NOT NULL COMMENT '发送者 ID',
    receiver_id INT UNSIGNED NOT NULL COMMENT '接收者 ID',
    message TEXT NOT NULL COMMENT '消息内容',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',
    FOREIGN KEY (sender_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (receiver_id) REFERENCES users(id) ON DELETE CASCADE
) COMMENT='离线消息表';

-- 创建 offline_files 表
CREATE TABLE offline_files (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT '文件记录 ID',
    sender_id INT UNSIGNED NOT NULL COMMENT '发送者 ID',
    receiver_id INT UNSIGNED NOT NULL COMMENT '接收者 ID',
    file_path VARCHAR(255) NOT NULL COMMENT '文件存储路径',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '文件发送时间',
    FOREIGN KEY (sender_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (receiver_id) REFERENCES users(id) ON DELETE CASCADE
) COMMENT='离线文件表';

-- 示例数据插入（可选）
-- 添加默认用户
INSERT INTO users (username, password) VALUES 
('test_user1', 'password_hash_1'),
('test_user2', 'password_hash_2');

