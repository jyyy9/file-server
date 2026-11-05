from pathlib import Path
from .db import DB

def main():
    db = DB()
    try:
        for path in sorted((Path(__file__).resolve().parents[1] / 'database/migrations').glob('*.sql')):
            version = int(path.name.split('_')[0])
            db.q('CREATE TABLE IF NOT EXISTS ds_schema_version(version INT PRIMARY KEY, applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB')
            if db.q('SELECT version FROM ds_schema_version WHERE version=?', version):
                continue
            sql = '\n'.join(line for line in path.read_text().splitlines() if not line.lstrip().startswith('--'))
            for statement in sql.split(';'):
                if statement.strip():
                    db.q(statement)
            print('applied', path.name)
    finally:
        db.close()

if __name__ == '__main__':
    main()
