.PHONY: all backend frontend clean install

all: backend frontend

backend:
	$(MAKE) -C backend all

frontend:
	cd frontend && npm ci && npm run build

clean:
	$(MAKE) -C backend clean
	rm -rf frontend/dist

install: all
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 backend/monitor-backend $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/etc/monitor-dashboard
	install -m 644 backend/config/monitor.conf $(DESTDIR)/etc/monitor-dashboard/config.toml
	install -d $(DESTDIR)/usr/share/monitor-dashboard/public
	cp -r frontend/dist/* $(DESTDIR)/usr/share/monitor-dashboard/public/

.PHONY: dev
dev:
	@echo "Start backend in one terminal: cd backend && make run"
	@echo "Start frontend in another: cd frontend && npm run dev"
