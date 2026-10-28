-- 初始化建库建表（微服务故障诊断 ReAct Agent）
CREATE DATABASE IF NOT EXISTS diag_agent
    DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE diag_agent;

-- 诊断任务主表
CREATE TABLE IF NOT EXISTS diagnosis_task (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    service_name  VARCHAR(128)  NOT NULL,
    symptom       TEXT          NOT NULL,
    severity      TINYINT       NOT NULL DEFAULT 2,          -- Severity 枚举：0=P0 1=P1 2=P2 3=P3
    status        TINYINT       NOT NULL DEFAULT 0,          -- TaskStatus 枚举：0=PENDING 1=RUNNING 2=SUCCEEDED 3=FAILED 4=TIMEOUT 5=CANCELLED
    max_steps     INT           NOT NULL DEFAULT 10,
    current_step  INT           NOT NULL DEFAULT 0,
    result        TEXT          NULL,
    error_message TEXT          NULL,
    confidence    DOUBLE        NULL,
    created_at    DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at    DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                                ON UPDATE CURRENT_TIMESTAMP(3),
    started_at    DATETIME(3)   NULL,
    finished_at   DATETIME(3)   NULL,
    KEY idx_status (status),
    KEY idx_created_at (created_at)
) ENGINE=InnoDB;

-- ReAct 单步轨迹表（Thought/Action/Observation）
CREATE TABLE IF NOT EXISTS react_trace (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    task_id      BIGINT UNSIGNED NOT NULL,
    step_no      INT            NOT NULL,
    thought      TEXT           NULL,
    action       VARCHAR(128)   NULL,
    action_input TEXT           NULL,
    observation  TEXT           NULL,
    created_at   DATETIME(3)    NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_task_step (task_id, step_no),
    CONSTRAINT fk_trace_task FOREIGN KEY (task_id)
        REFERENCES diagnosis_task (id) ON DELETE CASCADE
) ENGINE=InnoDB;
